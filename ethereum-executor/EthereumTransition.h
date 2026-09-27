/// @file EthereumTransition.h
/// @brief Ported evmone transaction lifecycle (eth/state/state.cpp
///        validate_transaction / transition / finalize) adapted to read/write
///        BCOS types directly — protocol::Transaction and the BlockHeader-
///        derived EthBlockInfo — against an EthereumState over BCOS storage.
///
/// No evmone::state::StateView, StateDiff or evmone::state::Transaction is
/// involved: the transaction is the bcos protocol::Transaction, validation and
/// execution read it directly, and the resulting BCOS receipt is produced
/// directly (no evmone TransactionReceipt intermediate).
///
/// Chain-specific behaviour (tx-type admission, fee withholding/settlement,
/// intrinsic gas) is delegated to the Policy template parameter — see
/// EthExecutionPolicy.h. The default EthL1Policy is the verbatim L1 logic;
/// an L2 (e.g. OP Stack) supplies its own policy instead of re-porting this
/// file.

#pragma once

#include "EVMSupport.h"
#include "EthExecutionPolicy.h"
#include "EthereumHost.h"
#include "EthereumState.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/BlobSchedule.h"
#include "bcos-framework/protocol/LogEntry.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-framework/protocol/TxGasModel.h"
#include "bcos-protocol/TransactionStatus.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <algorithm>
#include <evmc/evmc.hpp>
#include <evmone/constants.hpp>
#include <evmone/delegation.hpp>
#include <evmone_precompiles/secp256k1.hpp>
#include <optional>
#include <span>
#include <system_error>
#include <variant>

namespace bcos::executor_v1::eth
{
using evm::ErrorCode;
using evm::make_error_code;

// EIP-7840 blob schedule constants: the canonical table lives in
// bcos-framework/protocol/BlobSchedule.h (shared with the devp2p header
// validator, so the two cannot drift). The revision-keyed fallback below covers
// only chains that never stamp a per-block schedule into the ledger config:
// an evmc revision cannot express the post-Osaka BPO1/BPO2 schedule bumps, so a
// chain that schedules BPO1/BPO2 MUST go through the external-block verifier,
// which stamps the timestamp-resolved schedule into
// ledger::LedgerConfig::blobSchedule (see fillExecutionLedgerConfig).
inline evm::BlobParams toEvmBlobParams(protocol::BlobScheduleConfig const& schedule) noexcept
{
    return {.target = static_cast<uint16_t>(schedule.targetBlobs),
        .max = static_cast<uint16_t>(schedule.maxBlobs),
        .base_fee_update_fraction = static_cast<uint32_t>(schedule.baseFeeUpdateFraction)};
}

/// The EIP-7840 blob schedule for an EVM revision — the pre-BPO fallback
/// (empty pre-Cancun). Prefer blobParamsForBlock whenever a ledger config is
/// available.
inline evm::BlobParams blobParamsForRevision(evmc_revision rev) noexcept
{
    if (rev >= EVMC_PRAGUE)
        return toEvmBlobParams(protocol::PRAGUE_BLOB_SCHEDULE);  // Prague/Osaka.
    if (rev == EVMC_CANCUN)
        return toEvmBlobParams(protocol::CANCUN_BLOB_SCHEDULE);
    return {};
}

/// The blob params for the block being executed: the per-block schedule the
/// verifier stamped into the ledger config (BPO1/BPO2-aware) when present,
/// else the revision-keyed fallback.
inline evm::BlobParams blobParamsForBlock(
    ledger::LedgerConfig const& config, evmc_revision rev) noexcept
{
    if (auto const& schedule = config.blobSchedule();
        schedule.has_value() && schedule->maxBlobs != 0)
    {
        return toEvmBlobParams(*schedule);
    }
    return blobParamsForRevision(rev);
}

/// Transaction properties computed during the validation needed for the execution
/// (ported evmone::state::TransactionProperties).
struct EthTxProperties
{
    /// The amount of gas provided to the EVM for the transaction execution.
    int64_t execution_gas_limit = 0;

    /// The minimal amount of gas the transaction must use.
    int64_t min_gas_cost = 0;
};

/// A withdrawal applied at block finalization (EIP-4895, ported
/// evmone::state::Withdrawal).
struct EthWithdrawal
{
    uint64_t index = 0;
    uint64_t validator_index = 0;
    address recipient;
    uint64_t amount_in_gwei = 0;  ///< The amount is denominated in gwei.

    /// Returns withdrawal amount in wei.
    [[nodiscard]] uint256 get_amount() const noexcept
    {
        return uint256{amount_in_gwei} * 1'000'000'000;
    }
};

// ethToAddress is defined in bcos-framework/protocol/TxGasModel.h: the admission layer needs
// the same `to` decoding to decide create-vs-call, and it cannot link ethereum-executor.
// Re-imported so unqualified call sites in this header (and in the tests) resolve unchanged.
using bcos::protocol::ethToAddress;

namespace eth_transition_detail
{
/// Recover the EIP-7702 authority signer from a bcos Authorization via real
/// ecrecover (self-contained in EVMSupport.h).
inline std::optional<evmc::address> recoverAuthority(protocol::Authorization const& auth)
{
    return evm::recoverAuthority(auth);
}

// The transaction cost model (num_words / compute_tx_data_tokens / compute_access_list_cost /
// TransactionCost / compute_tx_intrinsic_cost) lives in bcos-framework/protocol/TxGasModel.h.
// Admission must reject a transaction whose gasLimit cannot cover the intrinsic cost, and it
// has to use this exact formula -- a second copy would drift at the next hard fork that moves
// EIP-7623's min_cost or the authorization-list cost. Re-imported so the call sites below and
// the qualified `eth_transition_detail::compute_tx_intrinsic_cost` uses resolve unchanged.
using bcos::protocol::gas::compute_access_list_cost;
using bcos::protocol::gas::compute_tx_data_tokens;
using bcos::protocol::gas::compute_tx_intrinsic_cost;
using bcos::protocol::gas::num_words;
using bcos::protocol::gas::TransactionCost;

inline evmc_message build_message(
    protocol::Transaction const& tx, int64_t execution_gas_limit) noexcept
{
    const auto to = ethToAddress(tx);
    const auto recipient = to.has_value() ? *to : evmc::address{};
    const auto input = tx.input();

    return {
        .kind = to.has_value() ? EVMC_CALL : EVMC_CREATE,
        .flags = 0,
        .depth = 0,
        .gas = execution_gas_limit,
        .recipient = recipient,
        .sender = ethSender(tx),
        .input_data = input.data(),
        .input_size = input.size(),
        .value = evm::toEvmcBE<evmc::uint256be>(tx.value()),
        .create2_salt = {},
        .code_address = recipient,
        .code = nullptr,
        .code_size = 0,
    };
}

/// EIP-7702: The single implementation both the eth path and the
/// opstack path share, ported to operate on EthereumState with a bcos
/// AuthorizationList. See EVMSupport.h's recoverAuthority for the recovery reference.
template <class Storage>
int64_t processAuthorizationList(
    EthereumState<Storage>& state, uint64_t chainId, protocol::Transaction const& tx)
{
    using evm::AUTHORIZATION_BASE_COST;
    using evm::AUTHORIZATION_EMPTY_ACCOUNT_COST;

    int64_t delegationRefund = 0;
    for (const auto& auth : tx.authorizationList())
    {
        // 1. Verify the chain id is either 0 or the chain's current ID.
        if (auth.chainId != 0 && auth.chainId != chainId)
            continue;

        // 2. Verify the nonce is less than 2**64 - 1.
        if (auth.nonce == EthAccount::NonceMax)
            continue;

        // 3. y_parity must be 0 or 1; s must be <= secp256k1n/2 (EIP-2).
        if (auth.v > 1)
            continue;
        if (auth.s > evm::SECP256K1N_OVER_2)
            continue;

        // 4. Always recover the signer via real ecrecover (never honour an
        //    attacker-supplied signer field).
        const auto signer = recoverAuthority(auth);
        if (!signer.has_value())
            continue;  // ecrecover failed → skip this authorization

        evmc::address target{};
        std::copy_n(auth.address.begin(), sizeof(evmc_address), target.bytes);

        // Get or create the authority account.
        EthAccount fresh;
        fresh.erase_if_empty = true;
        auto& authority = state.get_or_insert(*signer, std::move(fresh));

        // 5. Add authority to accessed_addresses (as defined in EIP-2929.)
        authority.access_status = EVMC_ACCESS_WARM;

        // 6. Verify the code of authority is either empty or already delegated.
        if (authority.code_hash != EthAccount::EMPTY_CODE_HASH &&
            !evmone::is_code_delegated(state.get_code(*signer)))
            continue;

        // 7. Verify the nonce of authority is equal to nonce.
        if (auth.nonce != authority.nonce)
            continue;

        // 8. Refund if authority existed before (empty implies non-existent).
        if (!authority.is_empty())
        {
            static constexpr auto EXISTING_AUTHORITY_REFUND =
                AUTHORIZATION_EMPTY_ACCOUNT_COST - AUTHORIZATION_BASE_COST;
            delegationRefund += EXISTING_AUTHORITY_REFUND;
        }

        // 9. As a special case, if address is 0 do not write the designation.
        //    Clear the account's code and reset the code hash to the empty hash.
        if (evmc::is_zero(target))
        {
            if (authority.code_hash != EthAccount::EMPTY_CODE_HASH)
            {
                authority.code_changed = true;
                authority.code.clear();
                authority.code_hash = EthAccount::EMPTY_CODE_HASH;
            }
        }
        // 10. Set the code of authority to be 0xef0100 || address.
        else
        {
            auto new_code = evmc::bytes(evmone::DELEGATION_MAGIC) +
                            evmc::bytes(target.bytes, target.bytes + sizeof(evmc_address));
            if (authority.code != new_code)
            {
                authority.code_changed = true;
                authority.code = std::move(new_code);
                authority.code_hash = evm::keccak256(authority.code);
            }
        }

        // 11. Increase the nonce of authority by one.
        ++authority.nonce;
    }
    return delegationRefund;
}
}  // namespace eth_transition_detail

/// Validates a transaction and computes its execution gas limit.
///
/// Ported from evmone state.cpp validate_transaction. Reads the bcos
/// Transaction directly; @p callParams carries the eth_call dry-run
/// normalization overrides (all empty / false for real execution).
/// @return Execution gas limit or transaction validation error.
template <class Storage, class Policy = EthL1Policy>
std::variant<EthTxProperties, std::error_code> validateTransaction(EthereumState<Storage>& state,
    EthBlockInfo const& block, protocol::Transaction const& tx, evmc_revision rev,
    int64_t blockGasLeft, int64_t blobGasLeft, EthCallParams const& callParams,
    Policy const& policy = Policy{})
{
    const auto txKind = tx.web3TypedTxKind();
    // The type-byte fork gating and the type-specific checks (blob / set-code)
    // are the chain policy's: an L2 admits a different set of type bytes (e.g.
    // OP's deposit 0x7e, blob txs rejected from Ecotone on).
    if (auto const typeError =
            policy.validateTxType(tx, txKind, rev, block, blobGasLeft, callParams))
        return *typeError;
    const auto gasLimit = effectiveGasLimit(tx, callParams);
    const auto nonce = effectiveNonce(tx, callParams);
    const auto maxGasPrice = ethMaxGasPrice(tx, callParams);
    const auto maxPriorityGasPrice = ethMaxPriorityGasPrice(tx, callParams);
    const auto hasTo = ethToAddress(tx).has_value();

    assert(maxPriorityGasPrice <= maxGasPrice);

    if (rev >= EVMC_OSAKA && gasLimit > evm::MAX_TX_GAS_LIMIT)
        return make_error_code(ErrorCode::MAX_GAS_LIMIT_EXCEEDED);

    if (gasLimit > blockGasLeft)
        return make_error_code(ErrorCode::GAS_LIMIT_REACHED);

    if (maxGasPrice < block.base_fee)
        return make_error_code(ErrorCode::FEE_CAP_LESS_THAN_BLOCKS);

    // We need some information about the sender so lookup the account in the state.
    const auto* const senderPtr = state.find(ethSender(tx));
    const auto senderNonce = senderPtr != nullptr ? senderPtr->nonce : 0;

    if (senderPtr != nullptr && senderPtr->code_hash != EthAccount::EMPTY_CODE_HASH &&
        !evmone::is_code_delegated(state.get_code(ethSender(tx))))
        return make_error_code(ErrorCode::SENDER_NOT_EOA);  // Origin must not be a contract
                                                            // (EIP-3607).

    if (senderNonce == EthAccount::NonceMax)  // Nonce value limit (EIP-2681).
        return make_error_code(ErrorCode::NONCE_HAS_MAX_VALUE);

    if (senderNonce < nonce)
        return make_error_code(ErrorCode::NONCE_TOO_HIGH);

    if (senderNonce > nonce)
        return make_error_code(ErrorCode::NONCE_TOO_LOW);

    // initcode size is limited by EIP-3860.
    if (rev >= EVMC_SHANGHAI && !hasTo && tx.input().size() > evmone::MAX_INITCODE_SIZE)
        return make_error_code(ErrorCode::INIT_CODE_SIZE_LIMIT_EXCEEDED);

    // Compute and check if sender has enough balance for the theoretical maximum transaction cost.
    // Widened to u512 so gasLimit * gasPrice + value cannot wrap, as the original intx::umul did.
    auto max_total_fee = bcos::u512(static_cast<uint64_t>(gasLimit)) * bcos::u512(maxGasPrice);
    max_total_fee += bcos::u512(tx.value());
    // Chain-level additions to the theoretical maximum cost (L1: the blob fee;
    // an L2 may add its L1 data fee) are the policy's.
    max_total_fee += policy.additionalMaxCost(tx, block, rev, callParams);
    const auto senderBalance = senderPtr != nullptr ? senderPtr->balance : uint256{};
    if (bcos::u512(senderBalance) < max_total_fee)
        return make_error_code(ErrorCode::INSUFFICIENT_FUNDS);

    const auto [intrinsic_cost, min_cost] = policy.intrinsicCost(rev, tx);
    if (gasLimit < std::max(intrinsic_cost, min_cost))
        return make_error_code(ErrorCode::INTRINSIC_GAS_TOO_LOW);

    const auto execution_gas_limit = gasLimit - intrinsic_cost;
    return EthTxProperties{execution_gas_limit, min_cost};
}

/// Executes a valid transaction (ported evmone transition()).
///
/// @param chainId the NODE's chain id — NOT tx.chain_id. EIP-7702 step 1
///                compares each authorization's chain id against it.
///
/// The resulting state changes are always written back to the BCOS storage
/// (matching the old executor, which applied the diff unconditionally). For a
/// dry-run (eth_call) the caller hands this a throwaway/forked view so nothing
/// real persists.
template <class Storage, class Policy = EthL1Policy>
task::Task<protocol::TransactionReceipt::Ptr> runTransaction(EthereumState<Storage>& state,
    EthBlockInfo const& block, BlockHashLookup blockHashLookup, protocol::Transaction const& tx,
    evmc_revision rev, evmc::VM& vm, EthTxProperties const& txProps, uint64_t chainId,
    EthCallParams const& callParams, protocol::TransactionReceiptFactory const& rf,
    int64_t blockNumber, Policy const& policy = Policy{})
{
    const auto gasLimit = effectiveGasLimit(tx, callParams);
    const auto sender = ethSender(tx);
    const auto to = ethToAddress(tx);

    auto& sender_acc = state.get_or_insert(sender);
    assert(sender_acc.nonce < EthAccount::NonceMax);  // Required for valid tx.
    ++sender_acc.nonce;                               // Bump sender nonce.

    // The NODE's chain id, never tx.chain_id: validate_transaction does not
    // check that field, so passing it would make EIP-7702 step 1 compare sender
    // input against sender input (pinned by TestEthereumAuthorizationList.cpp).
    const auto delegation_refund =
        eth_transition_detail::processAuthorizationList(state, chainId, tx);

    // The fee model (effective price, up-front withholding, post-execution
    // settlement) is the chain policy's — see EthExecutionPolicy.h.
    uint256 priority_gas_price;
    const auto effective_gas_price = policy.effectiveGasPrice(ethMaxGasPrice(tx, callParams),
        ethMaxPriorityGasPrice(tx, callParams), block, rev, priority_gas_price);
    const auto tx_max_cost =
        policy.withholdUpfront(sender_acc, tx, block, rev, gasLimit, effective_gas_price);

    EthereumHost<Storage, Policy> host{rev, vm, state, block, std::move(blockHashLookup),
        ethTxContextOf(tx, callParams), callParams, chainId, policy};

    sender_acc.access_status = EVMC_ACCESS_WARM;  // Tx sender is always warm.
    if (to.has_value())
        host.access_account(*to);
    for (const auto& entry : tx.web3AccessList())
    {
        evmc::address a{};
        std::copy_n(entry.account.begin(), sizeof(evmc_address), a.bytes);
        host.access_account(a);
        // A policy whose access_account does not materialize the account (OP's
        // always-warm precompile overrides) must still have one for
        // get_storage. No-op for L1: access_account already inserted it
        // (Berlin+; access lists cannot exist earlier).
        if (!entry.storageKeys.empty())
        {
            EthAccount fresh;
            fresh.erase_if_empty = true;
            state.get_or_insert(a, std::move(fresh));
        }
        for (const auto& sk : entry.storageKeys)
        {
            evmc_bytes32 key{};
            std::copy_n(sk.begin(), sizeof(evmc_bytes32), key.bytes);
            state.get_storage(a, key).access_status = EVMC_ACCESS_WARM;
        }
    }
    // EIP-3651: Warm COINBASE.
    if (rev >= EVMC_SHANGHAI)
        host.access_account(block.coinbase);

    auto message = eth_transition_detail::build_message(tx, txProps.execution_gas_limit);
    if (to.has_value())
    {
        if (const auto delegate = evmone::get_delegate_address(host, *to))
        {
            message.code_address = *delegate;
            message.flags |= EVMC_DELEGATED;
            host.access_account(message.code_address);
        }
    }

    const auto result = host.call(message);

    const auto gas_used = policy.template settleFees<Storage>(state, block, rev,
        txProps.min_gas_cost, gasLimit, result.gas_left, delegation_refund, result.gas_refund,
        tx_max_cost, effective_gas_price, priority_gas_price, sender_acc);

    auto receipt = policy.buildReceipt(host, result, gas_used, rf, blockNumber);

    co_await state.applyToStorage(rev);
    co_return receipt;
}

/// Finalize state after applying a "block" of transactions (ported evmone
/// finalize). Applies block reward to coinbase and withdrawals (post Shanghai);
/// empty touched accounts are cleaned up by applyToStorage (post Spurious Dragon).
template <class Storage>
task::Task<void> finalizeState(EthereumState<Storage>& state, evmc_revision rev,
    const address& coinbase, std::optional<uint64_t> blockReward,
    std::span<const EthWithdrawal> withdrawals)
{
    if (blockReward.has_value())
    {
        const auto reward = *blockReward;
        assert(reward % 32 == 0);  // Assume block reward is divisible by 32.
        const auto reward_by_32 = reward / 32;

        // No ommers are passed to the executor (matches the old wiring).
        state.touch(coinbase).balance += reward;
    }

    for (const auto& withdrawal : withdrawals)
        state.touch(withdrawal.recipient).balance += withdrawal.get_amount();

    co_await state.applyToStorage(rev);
}

}  // namespace bcos::executor_v1::eth
