/// @file OpEthDeposit.h
/// @brief The OP 0x7E deposit transaction on the ethereum-executor stack —
///        the bcos-evm-free counterpart of OpTransition.cpp's runDeposit and
///        OpstackExecutor.h's decodeDepositEnvelope.
///
/// Semantics are ported byte-for-byte (op-geth core/state_transition.go
/// preCheck/execute/innerExecute); see the function comments for the fork
/// gating. The deposit shell validates against an INLINE replica of
/// evmone validate_transaction's deposit-reachable checks (gas pool, nonce
/// ceiling, initcode size, intrinsic gas) rather than the generic
/// validateTransaction, because a deposit is not a protocol::Transaction and
/// op-geth masks the sender's balance/code for it (DepositValidationView).

#pragma once

#include <ethereum-executor/EVMSupport.h>
#include <ethereum-executor/EthereumHost.h>
#include <ethereum-executor/EthereumState.h>
#include <ethereum-executor/EthereumTransition.h>
#include <opstack-executor/OpExecutionPolicy.h>
#include <opstack-executor/OpEthReceipt.h>
#include <opstack-executor/OpForkSpec.h>
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/Result.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-framework/protocol/TransactionReceiptFactory.h>
#include <bcos-framework/protocol/TxGasModel.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Bloom.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Exceptions.h>
#include <cstdint>
#include <evmc/evmc.hpp>
#include <evmone/constants.hpp>
#include <evmone/delegation.hpp>
#include <limits>
#include <optional>
#include <stdexcept>

namespace bcos::executor_v1::opstack
{
namespace eth = bcos::executor_v1::eth;

DERIVE_BCOS_EXCEPTION(OpEthDepositValidationFailed);

/// OP 0x7E deposit transaction/receipt type (EIP-2718 typed envelope prefix).
inline constexpr uint8_t OP_DEPOSIT_TX_TYPE = 0x7e;

/// 0x7E deposit tx (not a protocol::Transaction). When mint has a value it is added
/// unconditionally to the from balance (nullopt = do not add); value is transferred
/// normally within the call — two independent fields. is_system_tx must be false after
/// Regolith. Amounts are bcos::u256 (the ethereum-executor's native uint256).
struct DepositTx
{
    evmc::bytes32 sourceHash;
    evmc::address from;
    std::optional<evmc::address> to;  // nullopt = contract creation (address derived from
                                      // from + pre-execution nonce)
    std::optional<bcos::u256> mint;   // nullopt = no mint (matches op-geth *big.Int nil)
    bcos::u256 value = 0;
    int64_t gasLimit = 0;
    bool isSystemTx = false;
    bcos::bytes data;
};

/// Deposit gas_limit exceeds remaining block gas (op-geth ErrGasLimitReached). Distinct
/// from OpBlockGasPoolFull (prepare-time normal-tx pool full).
struct OpEthDepositGasLimitReached : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

/// Decode `0x7e || rlp([sourceHash, from, to, mint, value, gas, isSystemTx, data])`.
/// Trust boundary: deposits are unsigned, so a peer can forge tars mint/value; only the
/// 0x7e envelope bytes bind those fields. Never read mint/value from the tars mirror.
// Named decodeOpDepositEnvelope (not decodeDepositEnvelope) to avoid colliding with the
// legacy bcos-evm decoder of the same name in OpstackExecutor.h under unity builds.
[[nodiscard]] inline DepositTx decodeOpDepositEnvelope(bcos::bytesConstRef env)
{
    namespace rlp = bcos::codec::rlp;
    auto fail = [](std::string const& msg) {
        BOOST_THROW_EXCEPTION(OpEthDepositValidationFailed{} << bcos::errinfo_comment(msg));
    };
    // decode-or-fail for the single-item field reads below.
    auto decodeField = [&fail](bcos::bytesRef& ref, auto& out, std::string const& msg) {
        if (!rlp::captureRlp([&] { rlp::decode(ref, out); }))
            fail(msg);
    };
    if (env.size() < 2 || env[0] != OP_DEPOSIT_TX_TYPE)
        fail("deposit envelope: not a 0x7e deposit");
    bcos::bytesRef body{const_cast<bcos::byte*>(env.data() + 1), env.size() - 1};
    auto header = rlp::tryDecodeHeader(body);
    if (!header || !header->isList)
        fail("deposit envelope: body must be an RLP list");
    if (header->payloadLength != body.size())
        fail("deposit envelope: trailing bytes after the RLP list");
    bcos::bytesRef items(body.data(), header->payloadLength);

    bcos::crypto::HashType sourceHash;
    bcos::Address from;
    if (auto const r = rlp::captureRlp([&] { rlp::decodeItems(items, sourceHash, from); }); !r)
        fail("deposit envelope: sourceHash/from decode failed: " + r.error().message);

    DepositTx dep;
    std::copy_n(sourceHash.begin(), sizeof(evmc::bytes32), dep.sourceHash.bytes);
    std::copy_n(from.begin(), sizeof(evmc::address), dep.from.bytes);

    // to: empty RLP item = contract creation (same convention as every Ethereum tx type)
    if (items.empty())
        fail("deposit envelope: missing to field");
    if (items[0] == rlp::BYTES_HEAD_BASE)
    {
        items = items.getCroppedData(1);
    }
    else
    {
        bcos::Address to{};
        decodeField(items, to, "deposit envelope: to decode failed");
        evmc::address ta{};
        std::copy_n(to.begin(), sizeof(evmc::address), ta.bytes);
        dep.to = ta;
    }
    // mint: empty RLP item = no mint (op-geth encodes nil *big.Int as the empty item; on the wire
    // nil and zero are the same 0x80, so nullopt matches op-geth's decode-side behavior)
    if (items.empty())
        fail("deposit envelope: missing mint field");
    if (items[0] == rlp::BYTES_HEAD_BASE)
    {
        items = items.getCroppedData(1);
    }
    else
    {
        // Width (≤32 bytes) and canonicality are enforced by the rlp integer decoder
        // (bcos-codec tryDecode(UnsignedIntegral&)) — the decode below rejects a
        // non-canonical or over-wide item, and decodeField maps it to the same
        // OpEthDepositValidationFailed as every other field fault.
        bcos::u256 m{0};
        decodeField(items, m, "deposit envelope: mint decode failed");
        dep.mint = m;
    }
    // value (u256): width + canonicality carried by the rlp decoder (over-wide would
    // otherwise truncate silently).
    decodeField(items, dep.value, "deposit envelope: value decode failed");
    // gas: width + canonicality from the rlp decoder, then int64 range. Over-range would
    // wrap to -1.
    uint64_t gas = 0;
    decodeField(items, gas, "deposit envelope: gas decode failed");
    if (gas > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
        fail("deposit envelope: gas exceeds int64 range");
    dep.gasLimit = static_cast<int64_t>(gas);
    // isSystemTx: 0 or 1 only, matching op-geth decodeBool. Decoded as uint64 because
    // the bool overload rejects the empty-item false.
    uint64_t isSystemTxValue = 0;
    decodeField(items, isSystemTxValue, "deposit envelope: isSystemTx decode failed");
    if (isSystemTxValue > 1)
        fail("deposit envelope: isSystemTx must be 0 or 1");
    dep.isSystemTx = isSystemTxValue != 0;
    decodeField(items, dep.data, "deposit envelope: data decode failed");
    if (!items.empty())
        fail("deposit envelope: trailing bytes inside the RLP list");
    return dep;
}

/// Execute one 0x7E deposit (ported OpTransition.cpp runDeposit): skip buy-gas; add
/// balance when mint has a value; still deduct intrinsic + the EIP-7623 floor; both
/// failure paths retain the mint and force-increment the nonce; gasLimit exceeding
/// blockGasLeft throws OpEthDepositGasLimitReached (op-geth ErrGasLimitReached,
/// block-level error). The receipt carries deposit_nonce/deposit_receipt_version via
/// setOpStackMeta and effectiveGasPrice "0x0". The state changes are written back via
/// applyToStorage, matching runTransaction's all-or-nothing write discipline.
///
/// Deposits are exempt from the EIP-7825 per-tx gas cap that Karst's Osaka base enforces
/// for normal transactions (docs.optimism.io/notices/upgrade-19): the intrinsic-gas
/// check runs at min(spec.rev, EVMC_PRAGUE); execution runs at the real spec.rev.
///
/// Fork gating (op-geth state_transition.go preCheck/execute/innerExecute):
///  * is_system_tx: Regolith+ (regolith_deposit_fixes) throws std::runtime_error
///    (op-geth ErrSystemTxNotSupported, block-level error). Pre-Regolith it marks the
///    deposit unmetered: the block gas pool is never checked or charged and the receipt
///    reports gasUsed = 0.
///  * Receipt gasUsed: Regolith+ reports the actual gas used; pre-Regolith a metered
///    deposit always reports its full gasLimit (success, EVM revert, or entry failure
///    alike — matches the pool, which was charged gasLimit and gets nothing back).
///  * Nonce: incremented on every outcome (success, EVM revert, entry failure) under
///    EVERY fork — spec deposits.md "Nonce Handling".
template <class Storage>
task::Task<protocol::TransactionReceipt::Ptr> opRunDeposit(eth::EthereumState<Storage>& state,
    eth::EthBlockInfo const& block, eth::BlockHashLookup blockHashLookup, DepositTx const& dep,
    OpForkSpec const& spec, evmc::VM& vm, uint64_t chainId, int64_t blockGasLeft,
    protocol::TransactionReceiptFactory const& rf, int64_t blockNumber)
{
    const auto rev = spec.rev;

    // op-geth state_transition.go preCheck: "Don't touch the gas pool for system transactions"
    // pre-Regolith; Regolith rejects them outright (ErrSystemTxNotSupported, block-level error).
    if (dep.isSystemTx && spec.regolith_deposit_fixes)
        throw std::runtime_error(
            "op deposit: is_system_tx not supported since Regolith (block error)");
    const bool preRegolith = !spec.regolith_deposit_fixes;
    const bool unmeteredSystemTx = dep.isSystemTx && preRegolith;
    // Pre-Regolith receipt gas accounting (op-geth innerExecute / execute failure branch):
    // "Record deposits as using all their gas (matches the gas pool). System Transactions are
    // special & are not recorded as using any gas (anywhere). Regolith changes this behaviour so
    // the actual gas used is reported."
    const int64_t preRegolithGasUsed = unmeteredSystemTx ? 0 : dep.gasLimit;

    auto& fromAcc = state.get_or_insert(dep.from);
    const uint64_t preNonce = fromAcc.nonce;
    // Mint wraps mod 2^256, matching op-geth AddBalance.
    if (dep.mint.has_value())
        fromAcc.balance += *dep.mint;

    // Inline replica of evmone validate_transaction's deposit-reachable checks
    // (the op-geth DepositValidationView masks the sender's balance and code, so
    // INSUFFICIENT_FUNDS / EIP-3607 never fire; the shell's nonce IS the sender's
    // pre-execution nonce, so NONCE_TOO_HIGH/LOW never fire). The revision is
    // clamped to Prague for the intrinsic check ONLY: deposits are exempt from
    // the EIP-7825 per-tx cap (upgrade-19 notice), and the only other Osaka-gated
    // validate rule (blob count) cannot fire — a deposit carries no blobs.
    const auto revValidate = std::min(rev, EVMC_PRAGUE);

    // A pre-Regolith system deposit never touches the block gas pool (op-geth preCheck):
    // the Bedrock L1-attributes deposit carries gasLimit 150M, above the block gas limit.
    if (!unmeteredSystemTx && dep.gasLimit > blockGasLeft)
        throw OpEthDepositGasLimitReached("op deposit: block gas limit reached (block error)");

    const auto [intrinsicCost, minGasCost] = protocol::gas::compute_tx_intrinsic_cost(revValidate,
        !dep.to.has_value(),
        std::span<const uint8_t>{dep.data.data(), dep.data.size()},
        /*access_list_cost=*/0, /*authorization_count=*/0);
    const bool validateFailed = fromAcc.nonce == eth::EthAccount::NonceMax ||  // EIP-2681
                                (revValidate >= EVMC_SHANGHAI && !dep.to.has_value() &&
                                    dep.data.size() > evmone::MAX_INITCODE_SIZE) ||
                                dep.gasLimit < std::max(intrinsicCost, minGasCost);

    evmc_status_code receiptStatus = EVMC_FAILURE;
    int64_t receiptGasUsed = preRegolith ? preRegolithGasUsed : dep.gasLimit;
    std::vector<protocol::LogEntry> logs;
    bcos::bytes outputBytes;

    if (validateFailed || fromAcc.balance < dep.value)
    {
        // Processing-level failure (op-geth Regolith, state_transition.go:486-513) and the
        // clause-6 value-affordability check (:578, against the post-mint balance): mint is
        // retained, nonce is force-incremented, gasUsed = gasLimit in full (:498).
        state.get(dep.from).nonce = preNonce + 1;
    }
    else
    {
        // Host::prepare_message does not bump the nonce itself for depth==0 messages (the
        // upstream evmone assumes the caller already bumped it, and CREATE address derivation
        // uses nonce-1 to obtain the "pre-execution" nonce). The bump is unconditional across
        // forks: deposits increment the sender nonce in every era (spec deposits.md "Nonce
        // Handling").
        assert(fromAcc.nonce < eth::EthAccount::NonceMax);
        ++fromAcc.nonce;

        const OpPolicy policy{spec, block};
        eth::EthTxContext txContext{
            .sender = dep.from,
            .maxGasPrice = 0,           // deposits pay nothing; the policy's zero-price
            .maxPriorityGasPrice = 0,   // override reports GASPRICE 0 to the EVM
            .blobHashes = {},
        };
        eth::EthereumHost<Storage, OpPolicy> host{rev, vm, state, block,
            std::move(blockHashLookup), std::move(txContext), eth::EthCallParams{}, chainId,
            policy};

        // The shared execution middle (ported runTxMessage): warm access → message →
        // EIP-7702 delegation resolution → host.call → refund/floor. No access list and
        // no delegation refund on a deposit.
        fromAcc.access_status = EVMC_ACCESS_WARM;  // Tx sender is always warm.
        if (dep.to.has_value())
            host.access_account(*dep.to);
        // EIP-3651: Warm COINBASE.
        if (rev >= EVMC_SHANGHAI)
            host.access_account(block.coinbase);

        const auto recipient = dep.to.has_value() ? *dep.to : evmc::address{};
        evmc_message message{
            .kind = dep.to.has_value() ? EVMC_CALL : EVMC_CREATE,
            .flags = 0,
            .depth = 0,
            .gas = dep.gasLimit - intrinsicCost,
            .recipient = recipient,
            .sender = dep.from,
            .input_data = dep.data.data(),
            .input_size = dep.data.size(),
            .value = eth::evm::toEvmcBE<evmc::uint256be>(dep.value),
            .create2_salt = {},
            .code_address = recipient,
            .code = nullptr,
            .code_size = 0,
        };
        if (dep.to.has_value())
        {
            if (const auto delegate = evmone::get_delegate_address(host, *dep.to))
            {
                message.code_address = *delegate;
                message.flags |= EVMC_DELEGATED;
                host.access_account(message.code_address);
            }
        }

        auto execResult = host.call(message);
        receiptStatus = execResult.status_code;

        auto gasUsed = dep.gasLimit - execResult.gas_left;
        const auto maxRefundQuotient = rev >= EVMC_LONDON ? 5 : 2;
        const auto refundLimit = gasUsed / maxRefundQuotient;
        const auto refund = std::min(execResult.gas_refund, refundLimit);
        gasUsed -= refund;
        assert(gasUsed > 0);
        // EIP-7623: The gas used by the transaction must be at least the min_gas_cost.
        gasUsed = std::max(gasUsed, minGasCost);
        receiptGasUsed = preRegolith ? preRegolithGasUsed : gasUsed;

        for (auto const& l : host.take_logs())
        {
            bcos::bytes addr(l.addr.bytes, l.addr.bytes + sizeof(evmc_address));
            bcos::h256s topics;
            for (auto const& t : l.topics)
                topics.emplace_back(bcos::bytesConstRef(t.bytes, sizeof(evmc_bytes32)));
            bcos::bytes data(l.data.begin(), l.data.end());
            logs.emplace_back(std::move(addr), std::move(topics), std::move(data));
        }
        // The attributes deposit's own return data (normally empty: it CALLs L1Block with a
        // void return). Guard the null-pointer case: evmone sets output_data=nullptr /
        // output_size=0 for a void-return call; `nullptr + 0` is UB.
        if (execResult.output_size != 0)
        {
            outputBytes.assign(
                execResult.output_data, execResult.output_data + execResult.output_size);
        }
    }

    if (receiptGasUsed < 0)
        throw std::runtime_error("opRunDeposit: negative gas_used");
    auto out = rf.createReceipt(bcos::u256{static_cast<uint64_t>(receiptGasUsed)},
        !dep.to.has_value() ? toFiscoContractAddress(dep.from, preNonce) : std::string{},
        std::move(logs), toFiscoOpStatus(receiptStatus),
        bcos::bytesConstRef{outputBytes.data(), outputBytes.size()}, blockNumber);
    const auto bloom = bcos::getLogsBloom(out->logEntries());
    out->setLogsBloom(bcos::bytesConstRef{bloom.data(), bloom.size()});

    // Deposit nonce/version on opStackMeta (op-geth deposit receipt has no L1/operator/DA
    // fields); effectiveGasPrice is 0 for deposits (op-geth emits "0x0").
    // Fork gating (op-geth state_processor.go MakeReceipt / spec deposits.md "Deposit Receipt"):
    // deposit_nonce is recorded from Regolith on (pre-Regolith receipts omit it);
    // deposit_receipt_version=1 joins at Canyon.
    bcos::protocol::OpStackReceiptMeta meta;
    if (spec.regolith_deposit_fixes)
        meta.deposit_nonce = preNonce;
    if (spec.has_deposit_receipt_version)
        meta.deposit_receipt_version = 1;
    out->setOpStackMeta(std::move(meta));
    out->setEffectiveGasPrice("0x0");

    co_await state.applyToStorage(rev);
    co_return out;
}
}  // namespace bcos::executor_v1::opstack
