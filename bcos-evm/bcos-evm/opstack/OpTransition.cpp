#include <bcos-evm/adapter/StateDiffSanitize.h>
#include <bcos-evm/eth/Eip7702Recover.h>
#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpHost.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <bcos-evm/opstack/RollupCost.h>
#include <bcos-framework/protocol/LogEntry.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <algorithm>
#include <bcos-evm/eth/state/bloom_filter.hpp>
#include <bcos-evm/eth/state/errors.hpp>
#include <bcos-evm/eth/state/host.hpp>
#include <cassert>
// TODO(eth-utils-removal): several sections of this file are copied verbatim from
// evmone test/state/state.cpp (official v0.21.0); replacing them means rewriting the copied
// surface and re-verifying the equivalence claims.
#include <evmone/delegation.hpp>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>

using namespace intx;

namespace bcos::evm::opstack
{
using bcos::evm::eth::processAuthorizationList;

namespace
{
evmc_message build_message(
    const evmone::state::Transaction& tx, int64_t execution_gas_limit) noexcept
{
    const auto recipient = tx.to.has_value() ? *tx.to : evmc::address{};

    return {
        .kind = tx.to.has_value() ? EVMC_CALL : EVMC_CREATE,
        .flags = 0,
        .depth = 0,
        .gas = execution_gas_limit,
        .recipient = recipient,
        .sender = tx.sender,
        .input_data = tx.data.data(),
        .input_size = tx.data.size(),
        .value = intx::be::store<evmc::uint256be>(tx.value),
        .create2_salt = {},
        .code_address = recipient,
        .code = nullptr,
        .code_size = 0,
    };
}

// ---- evmone Log -> bcos::protocol::LogEntry projection (formerly OpReceiptMap.h) ----

/// bytes-for-bytes copy of a 20-byte address into a bcos::bytes payload — LogEntry::address()
/// reinterprets its stored bytes as raw binary (not a hex string), see LogEntry.h:41, so this is
/// a plain byte copy, not a hex encode.
inline bcos::bytes mapOpLogAddress(const evmc::address& addr)
{
    return bcos::bytes(std::begin(addr.bytes), std::end(addr.bytes));
}

inline bcos::h256 mapOpTopic(const evmc::bytes32& topic)
{
    return bcos::h256(reinterpret_cast<const bcos::byte*>(topic.bytes), sizeof(topic.bytes));
}

inline bcos::protocol::LogEntries mapOpLogs(const std::vector<evmone::state::Log>& logs)
{
    bcos::protocol::LogEntries out;
    out.reserve(logs.size());
    for (const auto& log : logs)
    {
        bcos::h256s topics;
        topics.reserve(log.topics.size());
        for (const auto& topic : log.topics)
            topics.push_back(mapOpTopic(topic));
        out.emplace_back(mapOpLogAddress(log.addr), std::move(topics),
            bcos::bytes(log.data.begin(), log.data.end()));
    }
    return out;
}

/// Local helper: intx::uint256 → bcos::u256, full-width big-endian conversion. intx only exposes
/// an explicit low-64-bit cast operator, so a plain `static_cast<bcos::u256>` would silently drop
/// the high 192 bits — go through a big-endian byte store + bcos::fromBigEndian instead (same
/// pattern as ReceiptResponse.cpp's decode path).
inline bcos::u256 intxToBcosU256(intx::uint256 const& val)
{
    auto be = intx::be::store<evmc::uint256be>(val);
    return bcos::fromBigEndian<bcos::u256>(
        bcos::bytesConstRef{reinterpret_cast<bcos::byte const*>(be.bytes), sizeof(be.bytes)});
}

/// Convert the execution layer's opstack::OpReceiptMeta into the framework layer's
/// bcos::protocol::OpStackReceiptMeta (the typed view over the tars opStackMeta hex-string
/// fields). uint256 fields use intxToBcosU256 (full-width); uint64/uint32 scalar fields are
/// assigned directly. Presence is preserved per-field. effective_gas_price is deliberately NOT
/// carried here — it lands on the receipt's top-level effectiveGasPrice field (op-geth api.go:1775
/// emits it at the top level, not inside the OP extension object).
inline bcos::protocol::OpStackReceiptMeta toOpStackMeta(const OpReceiptMeta& meta)
{
    bcos::protocol::OpStackReceiptMeta out;
    if (meta.l1_gas_price)
        out.l1_gas_price = intxToBcosU256(*meta.l1_gas_price);
    if (meta.l1_fee)
        out.l1_fee = intxToBcosU256(*meta.l1_fee);
    if (meta.l1_blob_base_fee)
        out.l1_blob_base_fee = intxToBcosU256(*meta.l1_blob_base_fee);
    if (meta.l1_base_fee_scalar)
        out.l1_base_fee_scalar =
            *meta.l1_base_fee_scalar;  // uint32 -> uint64 scalar, direct assignment
    if (meta.l1_blob_base_fee_scalar)
        out.l1_blob_base_fee_scalar = *meta.l1_blob_base_fee_scalar;
    if (meta.operator_fee_scalar)
        out.operator_fee_scalar = *meta.operator_fee_scalar;
    if (meta.operator_fee_constant)
        out.operator_fee_constant = *meta.operator_fee_constant;
    if (meta.da_footprint_gas_scalar)
        out.da_footprint_gas_scalar = *meta.da_footprint_gas_scalar;
    if (meta.da_footprint)
        out.da_footprint = *meta.da_footprint;
    if (meta.l1_gas_used)
        out.l1_gas_used = *meta.l1_gas_used;
    if (meta.operator_fee)
        out.operator_fee = intxToBcosU256(*meta.operator_fee);
    return out;
}

/// FISCO status convention: 0 == success (precompiled contracts / BlockExecutive.cpp precedent).
/// evmc_status_code's finer-grained failure taxonomy collapses to a single non-zero code, the same
/// "status-only mapping" scope OpReceiptMap.h specified.
inline int32_t toFiscoStatus(evmc_status_code status) noexcept
{
    return status == EVMC_SUCCESS ? 0 : 1;
}

/// Contract-creation address for a top-level transaction, in the FISCO receipt's string form
/// (40-char lowercase hex, NO "0x" — the RPC layer adds "0x" + checksum). Mirrors op-geth
/// state_processor.go MakeReceipt: a creation tx (to == nil) records
/// `CreateAddress(from, nonce)` where nonce is the sender's pre-execution account nonce —
/// tx.Nonce() for a normal tx, statedb.GetNonce(from) (i.e. preNonce) for a Regolith+ deposit.
/// contractAddress is an RPC-only field: it is NOT part of the OP consensus receipt RLP (status /
/// cumGas / bloom / logs / deposit nonce+version), so filling it never affects receiptsRoot or
/// stateRoot.
inline std::string toFiscoContractAddress(const evmc::address& sender, uint64_t nonce)
{
    const auto addr = evmone::state::compute_create_address(sender, nonce);
    // evmc::address::bytes is a plain uint8_t[20]; brace-init a bytes_view (same as
    // TestPrinters.h).
    return std::string(evmc::hex({addr.bytes, sizeof(addr.bytes)}));
}

/// Project one executed transaction onto a bcos::protocol::TransactionReceipt: gasUsed/status/logs
/// come from the evmone receipt, blockNumber from the executing block info, and the 256-byte
/// logsBloom is copied onto the FISCO receipt's own logsBloom field (the block-level bloom is
/// rebuilt from receipts at seal time in part 3; the receipts-root leaf encoding likewise
/// returns with the block-seal wiring — encodeReceiptForRoot was removed with OpReceipt.h).
/// contractAddress is derived by the caller (toFiscoContractAddress) for creation txs, else
/// empty.
inline bcos::protocol::TransactionReceipt::Ptr makeFiscoReceipt(
    const bcos::protocol::TransactionReceiptFactory::Ptr& receiptFactory,
    const evmone::state::TransactionReceipt& evmoneReceipt, const evmone::state::BlockInfo& block,
    bcos::bytesConstRef output, std::string contractAddress = {})
{
    // gas_used 是 int64_t，运行时检查非负防 cast 回绕（evmone 保证；负值仅理论可达，但
    // NDEBUG 下 assert 会消失，共识面不容忍回绕）。
    if (evmoneReceipt.gas_used < 0)
        throw std::runtime_error("opTransition: negative gas_used");
    auto out = receiptFactory->createReceipt(
        bcos::u256{static_cast<uint64_t>(evmoneReceipt.gas_used)}, std::move(contractAddress),
        mapOpLogs(evmoneReceipt.logs), toFiscoStatus(evmoneReceipt.status), output,
        static_cast<bcos::protocol::BlockNumber>(block.number));
    out->setLogsBloom(bcos::bytesConstRef{
        evmoneReceipt.logs_bloom_filter.bytes, sizeof(evmoneReceipt.logs_bloom_filter.bytes)});
    return out;
}
}  // namespace

RunTxResult runTxMessage(evmone::state::State& state, OpHost& host,
    const evmone::state::Transaction& tx, evmc_revision rev, const evmc::address& coinbase,
    int64_t execution_gas_limit, int64_t min_gas_cost, int64_t delegation_refund)
{
    state.get(tx.sender).access_status = EVMC_ACCESS_WARM;
    if (tx.to.has_value())
        host.access_account(*tx.to);
    for (const auto& [a, storage_keys] : tx.access_list)
    {
        host.access_account(a);
        if (storage_keys.empty())
            continue;
        // OpHost::access_account returns early for override-table addresses without inserting
        // the account; State::get_storage requires it to exist.
        evmone::state::Account fresh;
        fresh.erase_if_empty = true;
        state.get_or_insert(a, std::move(fresh));
        for (const auto& key : storage_keys)
            state.get_storage(a, key).access_status = EVMC_ACCESS_WARM;
    }
    if (rev >= EVMC_SHANGHAI)
        host.access_account(coinbase);

    auto message = build_message(tx, execution_gas_limit);
    if (tx.to.has_value())
    {
        if (const auto delegate = evmone::get_delegate_address(host, *tx.to))
        {
            message.code_address = *delegate;
            message.flags |= EVMC_DELEGATED;
            host.access_account(message.code_address);
        }
    }

    auto result = host.call(message);

    auto gas_used = tx.gas_limit - result.gas_left;

    const auto max_refund_quotient = rev >= EVMC_LONDON ? 5 : 2;
    const auto refund_limit = gas_used / max_refund_quotient;
    const auto refund = std::min(delegation_refund + result.gas_refund, refund_limit);
    gas_used -= refund;
    assert(gas_used > 0);

    gas_used = std::max(gas_used, min_gas_cost);

    return {std::move(result), gas_used};
}

OpReceiptMeta deriveOpReceiptMeta(const OpTxProperties& props, intx::uint256 operator_fee_at_used,
    bool fill_operator_scalars) noexcept
{
    const auto& fee = props.fee;
    OpReceiptMeta m;
    m.l1_gas_price = fee.l1_base_fee;
    m.l1_blob_base_fee = fee.blob_base_fee;
    m.l1_base_fee_scalar = fee.base_fee_scalar;
    m.l1_blob_base_fee_scalar = fee.blob_base_fee_scalar;
    m.l1_fee = props.l1_cost;
    // L1 calldata gas used. Ecotone: bedrockCalldataGasUsed on the envelope (zeroes*4 + ones*16),
    // snapped into props at validate time. Fjord+ (op-geth rollup_cost.go:623-624):
    //   L1GasUsed = estimatedDASizeScaled(fastLzSize) * 16 / 1e6.
    // deriveOPStackFields emits it on every non-deposit receipt; ecotone_calldata_gas_used is
    // unset under Fjord+, where flz_len drives the formula. Bounded: with uint32 flz the scaled
    // term is ≤ 5.8e10, far below uint64 max, so the cast cannot wrap.
    if (props.ecotone_calldata_gas_used.has_value())
        m.l1_gas_used = *props.ecotone_calldata_gas_used;
    else
        m.l1_gas_used =
            static_cast<uint64_t>(estimatedDaSizeScaled(props.flz_len) * 16 / 1'000'000);
    if (props.has_operator_fee)
    {
        m.operator_fee = operator_fee_at_used;
        if (fill_operator_scalars &&
            (fee.operator_fee_scalar != 0 || fee.operator_fee_constant != 0))
        {
            m.operator_fee_scalar = fee.operator_fee_scalar;
            m.operator_fee_constant = fee.operator_fee_constant;
        }
    }
    if (props.has_da_footprint)
    {
        const auto scalar = static_cast<uint64_t>(fee.da_footprint_gas_scalar);
        m.da_footprint_gas_scalar = scalar;
        m.da_footprint = estimatedDaSizeFromFlz(props.flz_len) * scalar;
    }
    return m;
}

bcos::protocol::TransactionReceipt::Ptr opTransition(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    const evmone::state::Transaction& tx, const OpForkConfig& cfg, evmc::VM& vm,
    const OpTxProperties& props, uint64_t chainId,
    const bcos::protocol::TransactionReceiptFactory::Ptr& receiptFactory,
    evmone::state::StateDiff& outStateDiff)
{
    const auto rev = cfg.rev;
    evmone::state::State state{view};

    auto& sender_acc = state.get_or_insert(tx.sender);
    assert(sender_acc.nonce < evmone::state::Account::NonceMax);
    ++sender_acc.nonce;

    const auto delegation_refund = processAuthorizationList(state, chainId, tx.authorization_list);

    const auto base_fee = (rev >= EVMC_LONDON) ? block.base_fee : 0;
    assert(tx.max_gas_price >= base_fee);
    assert(tx.max_gas_price >= tx.max_priority_gas_price);
    const auto priority_gas_price =
        std::min(tx.max_priority_gas_price, tx.max_gas_price - base_fee);
    const auto effective_gas_price = base_fee + priority_gas_price;

    assert(effective_gas_price <= tx.max_gas_price);
    const auto tx_max_cost = tx.gas_limit * effective_gas_price;

    // The three subtractions below are unchecked, and what makes them safe lives in another
    // function: opValidate's 512-bit cap has already verified
    //   balance >= gasLimit*maxGasPrice + value + l1Cost + opCost(gasLimit).
    // That comparison always runs (there is no skip flag). eth_call/estimateGas wrap this
    // view with CallSimulationView so the sender reports uint256::max() and the cap still
    // holds. That is a runtime comparison, not an assert, so it still holds under NDEBUG.
    // What is deducted here is gasLimit*effective + l1 + opCost(gasLimit) with
    // effective <= maxGasPrice, so the amount taken never exceeds the bound that was
    // checked. Changing either side means changing both.
    sender_acc.balance -= tx_max_cost;

    sender_acc.balance -= props.l1_cost;
    if (props.has_operator_fee)
        sender_acc.balance -= props.operator_cost_at_gas_limit;

    OpHost host{cfg.rev, vm, state, block, hashes, tx, chainId, cfg.precompiles};

    auto outcome = runTxMessage(state, host, tx, rev, block.coinbase,
        props.props.execution_gas_limit, props.props.min_gas_cost, delegation_refund);
    const auto gas_used = outcome.gas_used;

    sender_acc.balance += tx_max_cost - gas_used * effective_gas_price;
    state.touch(block.coinbase).balance += gas_used * priority_gas_price;

    // Operator fee: charge the vault with the SAME formula/params that priced the sender's
    // pre-charge (operator_cost_at_gas_limit), taken from the validate-time snapshot in props —
    // NOT from this call's cfg. This makes the two sides conserve even if validate and transition
    // straddle a fork boundary: cap = opCost(gas_limit, snapshot), used = opCost(gas_used,
    // snapshot); opCost is monotonic in gas and gas_used <= gas_limit, so cap >= used holds and
    // the refund never underflows. sender net = cap - (cap - used) = used = vault credit.
    const auto opAtUsed = props.has_operator_fee ?
                              computeOperatorCost(props.fee, static_cast<uint64_t>(gas_used),
                                  props.jovian_operator_formula) :
                              intx::uint256{0};
    state.touch(OP_BASE_FEE_VAULT).balance +=
        intx::uint256{static_cast<uint64_t>(gas_used)} * intx::uint256{base_fee};
    state.touch(OP_L1_FEE_VAULT).balance += props.l1_cost;
    if (props.has_operator_fee)
    {
        state.touch(OP_OPERATOR_FEE_VAULT).balance += opAtUsed;
        assert(props.operator_cost_at_gas_limit >= opAtUsed);
        sender_acc.balance += props.operator_cost_at_gas_limit - opAtUsed;
    }

    evmone::state::TransactionReceipt receipt{tx.type, outcome.result.status_code, gas_used, {},
        host.take_logs(), {}, bcos::evm::sanitizeStateDiff(view, state.build_diff(rev)), {}};

    receipt.logs_bloom_filter = evmone::state::compute_bloom_filter(receipt.logs);

    // Both flags come from the SAME validate-time snapshot that priced and charged this
    // transaction, never from this call's cfg: when the two disagree across a fork boundary (the
    // case OperatorFeeConservesWhenCfgDisagreesWithProps covers) the receipt must describe what
    // the sender was actually charged. deriveOpReceiptMeta takes no cfg at all, so there is no
    // second source of truth left to get this wrong.
    auto meta = deriveOpReceiptMeta(props, opAtUsed, /*fill_operator_scalars=*/true);

    // Guard output_data nullptr (void-return calls), matching runDeposit: `nullptr + 0` is UB.
    const bcos::bytes outputBytes{outcome.result.output_size != 0 ?
                                      bcos::bytes{outcome.result.output_data,
                                          outcome.result.output_data + outcome.result.output_size} :
                                      bcos::bytes{}};
    auto out = makeFiscoReceipt(receiptFactory, receipt, block,
        bcos::bytesConstRef{outputBytes.data(), outputBytes.size()},
        !tx.to.has_value() ? toFiscoContractAddress(tx.sender, tx.nonce) : std::string{});
    out->setOpStackMeta(toOpStackMeta(meta));
    // op-geth hexutil.Big: "0x" + lowercase hex, no leading zeros (api.go:1775, RPC top-level).
    out->setEffectiveGasPrice("0x" + intx::to_string(effective_gas_price, 16));
    // out-param written last: an exception in the projection above must not leave a diff to apply.
    outStateDiff = std::move(receipt.state_diff);
    return out;
}


// ---- tx validation (formerly OpValidate.cpp) ----

std::variant<OpTxProperties, std::error_code> opValidate(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::Transaction& tx,
    evmc::bytes_view signedTxEnvelope, const OpForkConfig& cfg, const OpFeeParams& fee,
    int64_t blockGasLeft)
{
    // Whitelist, not a blacklist. Transaction::Type has uint8_t as its underlying type and
    // validate_transaction's type switch (state.cpp:365-383) carries no default label, so EVERY
    // value above set_code — 0x05..0xFF, which includes the 0x7E deposit type — falls through
    // each revision gate and is validated as if it were legacy. Rejecting only 0x7E would close
    // one instance of that hole and leave the rest open.
    //
    // The consequence is a consensus one: rlp_encode emits the raw type byte as the typed
    // envelope prefix, so an accepted out-of-enum transaction contributes a leaf carrying that
    // prefix to the receipts root, priced under legacy rules. For 0x7E specifically it would
    // also skip runDeposit entirely — buying gas, charging L1 and operator fees and enforcing
    // the nonce, none of which a deposit gets — and emit a receipt without deposit_nonce or
    // deposit_receipt_version.
    if (tx.type == evmone::state::Transaction::Type::blob ||
        tx.type > evmone::state::Transaction::Type::set_code)
        return make_error_code(std::errc::not_supported);

    if (signedTxEnvelope.empty())
        return make_error_code(std::errc::invalid_argument);

    auto base = evmone::state::validate_transaction(view, block, tx, cfg.rev, blockGasLeft, 0);
    if (auto* err = std::get_if<std::error_code>(&base))
        return *err;

    uint32_t flzLen = 0;
    intx::uint256 l1Cost;
    if (cfg.has_ecotone_l1_formula)
    {
        l1Cost = computeL1Cost(fee, signedTxEnvelope, cfg);
    }
    else
    {
        flzLen = flzCompressLen(signedTxEnvelope);
        l1Cost = computeL1CostFromFlz(fee, flzLen, cfg);
    }
    const auto opCost = cfg.has_operator_fee ?
                            computeOperatorCost(fee, static_cast<uint64_t>(tx.gas_limit), cfg) :
                            intx::uint256{0};
    const auto acc = view.get_account(tx.sender);
    const auto balance = acc ? acc->balance : intx::uint256{0};
    // 512-bit like upstream validate_transaction ("The computation cannot overflow if done with
    // 512-bit precision", eth/state/state.cpp): a wrapping uint256 sum would let
    // gasLimit*maxGasPrice + value + l1Cost + opCost pass the cap when it exceeds 2^256, and
    // opTransition's unchecked balance subtractions would then underflow (mint). The asserts
    // there compile away under NDEBUG, so this comparison is the only guard they have.
    // Always run it. eth_call/estimateGas wrap the view (CallSimulationView) so the sender
    // reports uint256::max(); skipping the comparison used to leave validate_transaction's
    // INSUFFICIENT_FUNDS check and these subtractions unguarded.
    auto maxCost = intx::umul(intx::uint256{static_cast<uint64_t>(tx.gas_limit)}, tx.max_gas_price);
    maxCost += tx.value;
    maxCost += l1Cost;
    maxCost += opCost;
    if (intx::uint512{balance} < maxCost)
        return make_error_code(std::errc::result_out_of_range);

    OpTxProperties props{std::get<evmone::state::TransactionProperties>(base), l1Cost, opCost, fee,
        flzLen, cfg.has_operator_fee, cfg.has_jovian_operator_formula, cfg.has_da_footprint};
    // Under the Ecotone formula, snapshot the envelope's bedrockCalldataGasUsed (zeroes*4 +
    // ones*16) for deriveOpReceiptMeta to read as l1_gas_used -- preserving the no-cfg invariant.
    // Under Fjord+ it stays nullopt; l1_gas_used uses the Fjord formula on flz_len.
    props.ecotone_calldata_gas_used =
        cfg.has_ecotone_l1_formula ?
            std::optional<uint64_t>{bcos::evm::opstack::bedrockCalldataGasUsed(signedTxEnvelope)} :
            std::nullopt;
    return props;
}

std::variant<OpTxProperties, std::error_code> opValidateFromState(
    const evmone::state::StateView& view, const evmone::state::BlockInfo& block,
    const evmone::state::Transaction& tx, evmc::bytes_view signedTxEnvelope,
    const OpForkConfig& cfg, int64_t blockGasLeft)
{
    return opValidate(view, block, tx, signedTxEnvelope, cfg, loadOpFeeParams(view), blockGasLeft);
}

// ---- 0x7E deposit tx (formerly OpDepositTx.cpp) ----

namespace
{
/// View mask for validate_transaction: presents the depositor as "sufficient balance + empty
/// code", mirroring op-geth skipping the EOA/balance checks in preCheck for deposits. Value
/// affordability is checked explicitly inside runDeposit (against the post-mint balance,
/// op-geth state_transition.go:578 clause 6).
class DepositValidationView final : public evmone::state::StateView
{
public:
    DepositValidationView(
        const evmone::state::StateView& base, const evmc::address& sender) noexcept
      : m_base{base}, m_sender{sender}
    {}

    std::optional<Account> get_account(const evmc::address& addr) const noexcept override
    {
        auto acc = m_base.get_account(addr);
        if (addr != m_sender)
            return acc;
        if (!acc.has_value())
            acc.emplace();
        acc->balance = std::numeric_limits<intx::uint256>::max();  // mask INSUFFICIENT_FUNDS
        acc->code_hash = evmone::state::Account::EMPTY_CODE_HASH;  // mask EIP-3607
        return acc;
    }

    evmone::state::bytes get_account_code(const evmc::address& addr) const noexcept override
    {
        return addr == m_sender ? evmone::state::bytes{} : m_base.get_account_code(addr);
    }

    evmone::state::bytes32 get_storage(
        const evmc::address& addr, const evmone::state::bytes32& key) const noexcept override
    {
        return m_base.get_storage(addr, key);
    }

private:
    const evmone::state::StateView& m_base;
    evmc::address m_sender;
};
}  // namespace

bcos::protocol::TransactionReceipt::Ptr runDeposit(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    const DepositTx& dep, const OpForkConfig& cfg, evmc::VM& vm, uint64_t chainId,
    int64_t blockGasLeft, const bcos::protocol::TransactionReceiptFactory::Ptr& receiptFactory,
    evmone::state::StateDiff& outStateDiff)
{
    if (dep.is_system_tx)
        throw std::runtime_error("op deposit: is_system_tx not supported (block error)");

    evmone::state::State state{view};
    auto& fromAcc = state.get_or_insert(dep.from);
    const uint64_t preNonce = fromAcc.nonce;
    if (dep.mint.has_value())
        fromAcc.balance += *dep.mint;

    evmone::state::Transaction tx;
    tx.type = evmone::state::Transaction::Type::legacy;  // internal execution shell; receipt uses
                                                         // kDepositTxType
    tx.sender = dep.from;
    tx.to = dep.to;
    tx.gas_limit = dep.gas_limit;
    tx.value = dep.value;
    tx.data = dep.data;
    tx.max_gas_price = 0;
    tx.max_priority_gas_price = 0;
    tx.nonce = preNonce;

    // Deposit skips the fee cap validation; validate is used only to compute intrinsic gas / the
    // EIP-7623 floor.
    evmone::state::BlockInfo validateBlock = block;
    validateBlock.base_fee = 0;
    const DepositValidationView maskedView{view, dep.from};
    const auto props = evmone::state::validate_transaction(
        maskedView, validateBlock, tx, cfg.rev, blockGasLeft, 0);

    evmone::state::TransactionReceipt receipt;
    receipt.type = kDepositTxType;
    bcos::bytes outputBytes;  // deposit return data (empty on the failure paths below)

    if (const auto* err = std::get_if<std::error_code>(&props))
    {
        if (*err == evmone::state::make_error_code(evmone::state::GAS_LIMIT_REACHED))
            throw std::runtime_error("op deposit: block gas limit reached (block error)");
        // Processing-level failure (op-geth Regolith, state_transition.go:486-513):
        // mint is retained, nonce is force-incremented, gasUsed = gasLimit in full (:498).
        state.get(dep.from).nonce = preNonce + 1;
        receipt.status = EVMC_FAILURE;
        receipt.gas_used = dep.gas_limit;
    }
    else
    {
        const auto& p = std::get<evmone::state::TransactionProperties>(props);
        if (state.get(dep.from).balance < dep.value)
        {
            // op-geth clause 6 (state_transition.go:578): consensus-layer error → failed-deposit
            // branch (:486-513), gasUsed = gasLimit in full (:498). Same as a validate failure.
            state.get(dep.from).nonce = preNonce + 1;
            receipt.status = EVMC_FAILURE;
            receipt.gas_used = dep.gas_limit;
        }
        else
        {
            // Host::prepare_message does not bump the nonce itself for depth==0 messages (the
            // upstream evmone assumes the caller already bumped it, and CREATE address derivation
            // uses nonce-1 to obtain the "pre-execution" nonce) — retains the fix from 2327532.
            assert(fromAcc.nonce < evmone::state::Account::NonceMax);
            ++fromAcc.nonce;
            OpHost host{cfg.rev, vm, state, block, hashes, tx, chainId, cfg.precompiles};
            auto outcome = runTxMessage(state, host, tx, cfg.rev, block.coinbase,
                p.execution_gas_limit, p.min_gas_cost, /*delegation_refund=*/0);
            receipt.status = outcome.result.status_code;
            receipt.gas_used = outcome.gas_used;
            receipt.logs = host.take_logs();
            // The attributes deposit's own return data (normally empty: it CALLs L1Block with a
            // void return). Copied into a buffer because outcome.result is scoped to this branch.
            // Guard the null-pointer case: evmone sets output_data=nullptr/output_size=0 for a
            // void-return call; `nullptr + 0` is UB (harmless in practice, ill-formed).
            if (outcome.result.output_size != 0)
            {
                outputBytes.assign(outcome.result.output_data,
                    outcome.result.output_data + outcome.result.output_size);
            }
        }
    }
    receipt.logs_bloom_filter = evmone::state::compute_bloom_filter(receipt.logs);
    receipt.state_diff = bcos::evm::sanitizeStateDiff(view, state.build_diff(cfg.rev));

    auto out = makeFiscoReceipt(receiptFactory, receipt, block,
        bcos::bytesConstRef{outputBytes.data(), outputBytes.size()},
        !dep.to.has_value() ? toFiscoContractAddress(dep.from, preNonce) : std::string{});

    // Deposit nonce/version on opStackMeta (op-geth deposit receipt has no L1/operator/DA
    // fields); effectiveGasPrice is 0 for deposits (op-geth emits "0x0").
    bcos::protocol::OpStackReceiptMeta meta;
    meta.deposit_nonce = preNonce;
    meta.deposit_receipt_version = 1;
    out->setOpStackMeta(std::move(meta));
    out->setEffectiveGasPrice("0x0");
    // Write the out-param only after the projection above has fully succeeded (see opTransition).
    outStateDiff = std::move(receipt.state_diff);
    return out;
}
}  // namespace bcos::evm::opstack
