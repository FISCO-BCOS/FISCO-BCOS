#include <bcos-evm/adapter/StateDiffSanitize.h>
#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpHost.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/OpReceiptMeta.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <bcos-evm/opstack/RollupCost.h>
#include <algorithm>
#include <bcos-evm/eth/state/bloom_filter.hpp>
#include <bcos-evm/eth/state/hash_utils.hpp>
#include <cassert>
// TODO(eth-utils-removal): rlp(eth/utils)→bcos-codec RLP。本文件多段照抄自
// evmone test/state/state.cpp(官方 v0.21.0),替换即照抄面重写,须重验等价性宣称。
#include <bcos-evm/eth/utils/rlp.hpp>
#include <evmone/delegation.hpp>
#include <evmone_precompiles/secp256k1.hpp>
#include <optional>
#include <span>

using namespace intx;

namespace bcos::evm::opstack
{
namespace
{
/// Secp256k1's N/2 is the upper bound of the signature's s value.
constexpr auto SECP256K1N_OVER_2 = evmmax::secp256k1::Curve::ORDER / 2;
/// EIP-7702: The cost of authorization that sets delegation to an account that didn't exist before.
constexpr auto AUTHORIZATION_EMPTY_ACCOUNT_COST = 25000;
/// EIP-7702: The cost of authorization that sets delegation to an account that already exists.
constexpr auto AUTHORIZATION_BASE_COST = 12500;
/// EIP-7702 authorization magic byte (prefix of signing hash).
constexpr uint8_t kSetCodeMagic = 0x05;

/// Recover the authority address from an EIP-7702 authorization via ecrecover.
/// signing hash = keccak256(0x05 || rlp([chain_id, address, nonce]))
std::optional<evmc::address> recoverAuthority(const evmone::state::Authorization& auth)
{
    auto msg = evmone::bytes{kSetCodeMagic} +
               evmone::rlp::encode_tuple(auth.chain_id, auth.addr, auth.nonce);
    const auto h = evmone::keccak256(msg);
    const auto r = intx::be::store<evmc::bytes32>(auth.r);
    const auto s = intx::be::store<evmc::bytes32>(auth.s);
    return evmmax::secp256k1::ecrecover(std::span<const uint8_t, 32>{h.bytes, 32},
        std::span<const uint8_t, 32>{r.bytes, 32}, std::span<const uint8_t, 32>{s.bytes, 32},
        auth.v != 0);
}

int64_t process_authorization_list(evmone::state::State& state, uint64_t chain_id,
    const evmone::state::AuthorizationList& authorization_list)
{
    int64_t delegation_refund = 0;
    for (const auto& auth : authorization_list)
    {
        // 1. Verify the chain id is either 0 or the chain's current ID.
        if (auth.chain_id != 0 && auth.chain_id != chain_id)
            continue;

        // 2. Verify the nonce is less than 2**64 - 1.
        if (auth.nonce == evmone::state::Account::NonceMax)
            continue;

        // 3. Verify if the signer has been successfully recovered from the signature.
        //    authority = ecrecover(...)
        // y_parity must be 0 or 1 for EIP-7702/2930 signatures.
        if (auth.v > 1)
            continue;
        // s value must be less than or equal to secp256k1n/2, as specified in EIP-2.
        // Validated before ecrecover, as op-geth does (ValidateSignatureValues before Recover).
        if (auth.s > SECP256K1N_OVER_2)
            continue;

        // Recover signer: use pre-set signer if available (test shortcut); otherwise ecrecover.
        std::optional<evmc::address> signer = auth.signer;
        if (!signer.has_value())
            signer = recoverAuthority(auth);
        if (!signer.has_value())
            continue;  // ecrecover failed → skip this authorization

        // Get or create the authority account.
        // It is still empty at this point until nonce bump following successful authorization.
        auto& authority = state.get_or_insert(*signer, {.erase_if_empty = true});

        // 4. Add authority to accessed_addresses (as defined in EIP-2929.)
        authority.access_status = EVMC_ACCESS_WARM;

        // 5. Verify the code of authority is either empty or already delegated.
        if (authority.code_hash != evmone::state::Account::EMPTY_CODE_HASH &&
            !evmone::is_code_delegated(state.get_code(*signer)))
            continue;

        // 6. Verify the nonce of authority is equal to nonce.
        // In case authority does not exist in the trie, verify that nonce is equal to 0.
        if (auth.nonce != authority.nonce)
            continue;

        // 7. Add PER_EMPTY_ACCOUNT_COST - PER_AUTH_BASE_COST gas to the global refund counter
        // if authority exists in the trie.
        // Successful authorization validation makes an account non-empty.
        // We apply the refund only if the account has existed before.
        // We detect "exists in the trie" by inspecting _empty_ property (EIP-161) because _empty_
        // implies an account doesn't exist in the state (EIP-7523).
        if (!authority.is_empty())
        {
            static constexpr auto EXISTING_AUTHORITY_REFUND =
                AUTHORIZATION_EMPTY_ACCOUNT_COST - AUTHORIZATION_BASE_COST;
            delegation_refund += EXISTING_AUTHORITY_REFUND;
        }

        // As a special case, if address is 0 do not write the designation.
        // Clear the account's code and reset the account's code hash to the empty hash.
        if (evmc::is_zero(auth.addr))
        {
            if (authority.code_hash != evmone::state::Account::EMPTY_CODE_HASH)
            {
                authority.code_changed = true;
                authority.code.clear();
                authority.code_hash = evmone::state::Account::EMPTY_CODE_HASH;
            }
        }
        // 8. Set the code of authority to be 0xef0100 || address. This is a delegation designation.
        else
        {
            auto new_code =
                evmone::state::bytes(evmone::DELEGATION_MAGIC) + evmone::state::bytes(auth.addr);
            if (authority.code != new_code)
            {
                authority.code_changed = true;
                authority.code = std::move(new_code);
                authority.code_hash = evmone::keccak256(authority.code);
            }
        }

        // 9. Increase the nonce of authority by one.
        ++authority.nonce;
    }
    return delegation_refund;
}

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

}  // namespace

ExecOutcome runTxMessage(evmone::state::State& state, OpHost& host,
    const evmone::state::Transaction& tx, evmc_revision rev, const evmc::address& coinbase,
    int64_t execution_gas_limit, int64_t min_gas_cost, int64_t delegation_refund)
{
    state.get(tx.sender).access_status = EVMC_ACCESS_WARM;
    if (tx.to.has_value())
        host.access_account(*tx.to);
    for (const auto& [a, storage_keys] : tx.access_list)
    {
        host.access_account(a);
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

OpTxReceipt opTransition(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    const evmone::state::Transaction& tx, const OpForkConfig& cfg, evmc::VM& vm,
    const OpTxProperties& props, uint64_t chainId, evmc::bytes_view signedTxEnvelope)
{
    const auto rev = cfg.rev;
    evmone::state::State state{view};

    auto& sender_acc = state.get_or_insert(tx.sender);
    assert(sender_acc.nonce < evmone::state::Account::NonceMax);
    ++sender_acc.nonce;

    const auto delegation_refund =
        process_authorization_list(state, chainId, tx.authorization_list);

    const auto base_fee = (rev >= EVMC_LONDON) ? block.base_fee : 0;
    assert(tx.max_gas_price >= base_fee);
    assert(tx.max_gas_price >= tx.max_priority_gas_price);
    const auto priority_gas_price =
        std::min(tx.max_priority_gas_price, tx.max_gas_price - base_fee);
    const auto effective_gas_price = base_fee + priority_gas_price;

    assert(effective_gas_price <= tx.max_gas_price);
    const auto tx_max_cost = tx.gas_limit * effective_gas_price;

    sender_acc.balance -= tx_max_cost;

    sender_acc.balance -= props.l1_cost;
    if (cfg.has_operator_fee)
        sender_acc.balance -= props.operator_cost_at_gas_limit;

    OpHost host{cfg.rev, vm, state, block, hashes, tx, chainId, cfg.precompiles};

    auto outcome = runTxMessage(state, host, tx, rev, block.coinbase,
        props.props.execution_gas_limit, props.props.min_gas_cost, delegation_refund);
    const auto gas_used = outcome.gas_used;

    sender_acc.balance += tx_max_cost - gas_used * effective_gas_price;
    state.touch(block.coinbase).balance += gas_used * priority_gas_price;

    const auto opAtUsed = cfg.has_operator_fee ?
                              computeOperatorCost(props.fee, static_cast<uint64_t>(gas_used), cfg) :
                              intx::uint256{0};
    state.touch(OP_BASE_FEE_VAULT).balance +=
        intx::uint256{static_cast<uint64_t>(gas_used)} * intx::uint256{base_fee};
    state.touch(OP_L1_FEE_VAULT).balance += props.l1_cost;
    if (cfg.has_operator_fee)
    {
        state.touch(OP_OPERATOR_FEE_VAULT).balance += opAtUsed;
        sender_acc.balance += props.operator_cost_at_gas_limit - opAtUsed;
    }

    evmone::state::TransactionReceipt receipt{tx.type, outcome.result.status_code, gas_used, {},
        host.take_logs(), {}, bcos::evm::sanitizeStateDiff(view, state.build_diff(rev)), {}};

    receipt.logs_bloom_filter = evmone::state::compute_bloom_filter(receipt.logs);

    auto meta = deriveOpReceiptMeta(cfg, props.fee, props.flz_len, props.l1_cost, opAtUsed,
        /*fill_operator_scalars=*/true);
    return OpTxReceipt{std::move(receipt), meta};
}

OpTxReceipt opTransitionFromState(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    const evmone::state::Transaction& tx, const OpForkConfig& cfg, evmc::VM& vm,
    const OpTxProperties& props, uint64_t chainId, evmc::bytes_view signedTxEnvelope)
{
    return opTransition(view, block, hashes, tx, cfg, vm, props, chainId, signedTxEnvelope);
}
}  // namespace bcos::evm::opstack
