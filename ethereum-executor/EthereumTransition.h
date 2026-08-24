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

#pragma once

#include "EthereumHost.h"
#include "EthereumState.h"
#include "EVMSupport.h"
#include "bcos-framework/protocol/LogEntry.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-protocol/TransactionStatus.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <evmc/evmc.hpp>
#include <evmone/constants.hpp>
#include <evmone/delegation.hpp>
#include <evmone_precompiles/secp256k1.hpp>
#include <algorithm>
#include <optional>
#include <span>
#include <system_error>
#include <variant>

namespace bcos::executor_v1::eth
{
using evm::ErrorCode;
using evm::make_error_code;

// EIP-7840 blob schedule constants (target, max, base_fee_update_fraction).
// Shared by EthereumExecutor (blob_gas_left for validation) and the block-info
// builder (blob_base_fee computation) so the two cannot drift.
inline constexpr evm::BlobParams PRAGUE_BLOB_PARAMS{
    .target = 6, .max = 9, .base_fee_update_fraction = 5007716};
inline constexpr evm::BlobParams CANCUN_BLOB_PARAMS{
    .target = 3, .max = 6, .base_fee_update_fraction = 3338477};

/// The EIP-7840 blob schedule in effect for @p rev (empty for pre-Cancun).
inline evm::BlobParams blobParamsForRevision(evmc_revision rev) noexcept
{
    if (rev >= EVMC_PRAGUE)
        return PRAGUE_BLOB_PARAMS;  // Prague/Osaka.
    if (rev == EVMC_CANCUN)
        return CANCUN_BLOB_PARAMS;  // Cancun.
    return {};
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
    [[nodiscard]] intx::uint256 get_amount() const noexcept
    {
        return intx::uint256{amount_in_gwei} * 1'000'000'000;
    }
};

/// Resolve the recipient of a bcos Transaction (Ethereum addresses are
/// big-endian and right-aligned). std::nullopt means contract creation —
/// returned for an empty `to` or a malformed non-20-byte value.
inline std::optional<address> ethToAddress(protocol::Transaction const& tx)
{
    auto const& tb = tx.to();
    if (tb.empty())
        return std::nullopt;

    const bool has0x = tb.size() >= 2 && tb[0] == '0' && (tb[1] == 'x' || tb[1] == 'X');
    const bool is40Hex = tb.size() == sizeof(evmc_address) * 2 &&
                         std::all_of(tb.begin(), tb.end(), [](char c) {
                             return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                                    (c >= 'A' && c <= 'F');
                         });
    if (has0x || is40Hex)
    {
        // Hex-string form. Only a well-formed 20-byte address decodes to a
        // valid recipient.
        if (auto decoded = safeFromHex(tb); decoded && decoded->size() == sizeof(evmc_address))
        {
            address a{};
            std::copy(decoded->begin(), decoded->end(), a.bytes);
            return a;
        }
        return std::nullopt;
    }
    if (tb.size() == sizeof(evmc_address))
    {
        // Defensive fallback for raw 20-byte addresses.
        address a{};
        std::copy_n(tb.begin(), sizeof(evmc_address), a.bytes);
        return a;
    }
    // Anything else (short raw bytes, malformed hex) is contract creation.
    return std::nullopt;
}

namespace eth_transition_detail
{
/// Recover the EIP-7702 authority signer from a bcos Authorization via real
/// ecrecover (self-contained in EVMSupport.h).
inline std::optional<evmc::address> recoverAuthority(protocol::Authorization const& auth)
{
    return evm::recoverAuthority(auth);
}

constexpr int64_t num_words(size_t size_in_bytes) noexcept
{
    return static_cast<int64_t>((size_in_bytes + 31) / 32);
}

inline size_t compute_tx_data_tokens(evmc_revision rev, std::span<const uint8_t> data) noexcept
{
    const auto num_zero_bytes = static_cast<size_t>(std::ranges::count(data, 0));
    const auto num_nonzero_bytes = data.size() - num_zero_bytes;

    const size_t nonzero_byte_multiplier = rev >= EVMC_ISTANBUL ? 4 : 17;
    return (nonzero_byte_multiplier * num_nonzero_bytes) + num_zero_bytes;
}

inline int64_t compute_access_list_cost(const protocol::Web3AccessList& access_list) noexcept
{
    static constexpr auto ADDRESS_COST = 2400;
    static constexpr auto STORAGE_KEY_COST = 1900;

    int64_t cost = 0;
    for (const auto& entry : access_list)
        cost += ADDRESS_COST + static_cast<int64_t>(entry.storageKeys.size()) * STORAGE_KEY_COST;
    return cost;
}

struct TransactionCost
{
    int64_t intrinsic = 0;
    int64_t min = 0;
};

/// Compute the transaction intrinsic gas 𝑔₀ (Yellow Paper, 6.2) and minimal gas
/// (EIP-7623). Ported from evmone state.cpp.
inline TransactionCost compute_tx_intrinsic_cost(
    evmc_revision rev, protocol::Transaction const& tx) noexcept
{
    static constexpr auto TX_BASE_COST = 21000;    static constexpr auto TX_CREATE_COST = 32000;
    static constexpr auto DATA_TOKEN_COST = 4;
    static constexpr auto INITCODE_WORD_COST = 2;
    static constexpr auto TOTAL_COST_FLOOR_PER_TOKEN = 10;

    const auto is_create = !ethToAddress(tx).has_value();

    const auto create_cost = (is_create && rev >= EVMC_HOMESTEAD) ? TX_CREATE_COST : 0;

    const auto data = tx.input();
    const auto num_tokens =
        static_cast<int64_t>(compute_tx_data_tokens(rev, std::span<const uint8_t>{data.data(), data.size()}));
    const auto data_cost = num_tokens * DATA_TOKEN_COST;

    const auto access_list_cost = compute_access_list_cost(tx.web3AccessList());

    const auto auth_list_cost = static_cast<int64_t>(tx.authorizationList().size()) *
                                evm::AUTHORIZATION_EMPTY_ACCOUNT_COST;

    const auto initcode_cost = (is_create && rev >= EVMC_SHANGHAI) ?
                                   INITCODE_WORD_COST * num_words(data.size()) :
                                   0;

    const auto intrinsic_cost =
        TX_BASE_COST + create_cost + data_cost + access_list_cost + auth_list_cost + initcode_cost;

    // EIP-7623: Compute the minimum cost for the transaction by. If disabled, just use 0.
    const auto min_cost =
        rev >= EVMC_PRAGUE ? TX_BASE_COST + num_tokens * TOTAL_COST_FLOOR_PER_TOKEN : 0;

    return {intrinsic_cost, min_cost};
}

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
        .value = intx::be::store<evmc::uint256be>(evm::toIntxU256(tx.value())),
        .create2_salt = {},
        .code_address = recipient,
        .code = nullptr,
        .code_size = 0,
    };
}

/// EIP-7702: The single implementation both the eth path and (historically) the
/// opstack path share, ported to operate on EthereumState with a bcos
/// AuthorizationList. See bcos-evm Eip7702Recover.h for the reference.
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
        if (evm::toIntxU256(auth.s) > evm::SECP256K1N_OVER_2)
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
template <class Storage>
std::variant<EthTxProperties, std::error_code> validateTransaction(EthereumState<Storage>& state,
    EthBlockInfo const& block, protocol::Transaction const& tx, evmc_revision rev,
    int64_t blockGasLeft, int64_t blobGasLeft, EthCallParams const& callParams)
{
    const auto txKind = tx.web3TypedTxKind();
    // Reject unknown / out-of-range typed-tx kinds (only 0-4 exist). geth
    // rejects unknown type bytes at RLP decode; the port has no decode layer,
    // so without this a crafted kind (>=5) would skip both type gates below,
    // trip the maxPriorityGasPrice assert (debug) or silently run as legacy
    // (release), diverging from geth.
    if (txKind > 4)
        return make_error_code(ErrorCode::TX_TYPE_NOT_SUPPORTED);
    const auto gasLimit = effectiveGasLimit(tx, callParams);
    const auto nonce = effectiveNonce(tx, callParams);
    const auto maxGasPrice = ethMaxGasPrice(tx, callParams);
    const auto maxPriorityGasPrice = ethMaxPriorityGasPrice(tx, callParams);
    const auto hasTo = ethToAddress(tx).has_value();
    const auto& blobHashes = tx.blobVersionedHashes();

    switch (txKind)  // Validate "special" transaction types.
    {
    case 3:  // blob
        if (rev < EVMC_CANCUN)
            return make_error_code(ErrorCode::TX_TYPE_NOT_SUPPORTED);
        if (!hasTo)
            return make_error_code(ErrorCode::CREATE_BLOB_TX);
        if (blobHashes.empty())
            return make_error_code(ErrorCode::EMPTY_BLOB_HASHES_LIST);
        if (rev >= EVMC_OSAKA && blobHashes.size() > evm::MAX_TX_BLOB_COUNT)
            return make_error_code(ErrorCode::BLOB_GAS_LIMIT_EXCEEDED);

        assert(block.blob_base_fee.has_value());
        if (ethMaxBlobGasPrice(tx) < *block.blob_base_fee)
            return make_error_code(ErrorCode::BLOB_FEE_CAP_LESS_THAN_BLOCKS);

        if (std::ranges::any_of(blobHashes, [](const auto& h) { return h[0] != 0x01; }))
            return make_error_code(ErrorCode::INVALID_BLOB_HASH_VERSION);
        if (static_cast<uint64_t>(evm::GAS_PER_BLOB) * blobHashes.size() >
            static_cast<uint64_t>(blobGasLeft))
            return make_error_code(ErrorCode::BLOB_GAS_LIMIT_EXCEEDED);
        break;

    case 4:  // set_code
        if (rev < EVMC_PRAGUE)
            return make_error_code(ErrorCode::TX_TYPE_NOT_SUPPORTED);
        if (!hasTo)
            return make_error_code(ErrorCode::CREATE_SET_CODE_TX);
        if (tx.authorizationList().empty())
            return make_error_code(ErrorCode::EMPTY_AUTHORIZATION_LIST);
        break;

    default:;
    }

    switch (txKind)  // Validate the "regular" transaction type hierarchy.
    {
    case 4:  // set_code
    case 3:  // blob
    case 2:  // eip1559
        if (rev < EVMC_LONDON)
            return make_error_code(ErrorCode::TX_TYPE_NOT_SUPPORTED);

        if (maxPriorityGasPrice > maxGasPrice)
            return make_error_code(ErrorCode::TIP_GT_FEE_CAP);  // Priority gas price is too high.
        [[fallthrough]];

    case 1:  // access_list
        if (rev < EVMC_BERLIN)
            return make_error_code(ErrorCode::TX_TYPE_NOT_SUPPORTED);
        [[fallthrough]];

    case 0:;  // legacy
    }

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

    if (senderPtr != nullptr &&
        senderPtr->code_hash != EthAccount::EMPTY_CODE_HASH &&
        !evmone::is_code_delegated(state.get_code(ethSender(tx))))
        return make_error_code(ErrorCode::SENDER_NOT_EOA);  // Origin must not be a contract (EIP-3607).

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
    auto max_total_fee =
        intx::umul(intx::uint256(static_cast<uint64_t>(gasLimit)), maxGasPrice);
    max_total_fee += evm::toIntxU256(tx.value());

    if (txKind == 3)  // blob
    {
        const auto total_blob_gas =
            static_cast<uint64_t>(evm::GAS_PER_BLOB) * blobHashes.size();
        max_total_fee += intx::uint256(total_blob_gas) * ethMaxBlobGasPrice(tx);
    }
    const auto senderBalance = senderPtr != nullptr ? senderPtr->balance : intx::uint256{};
    if (senderBalance < max_total_fee)
        return make_error_code(ErrorCode::INSUFFICIENT_FUNDS);

    const auto [intrinsic_cost, min_cost] =
        eth_transition_detail::compute_tx_intrinsic_cost(rev, tx);
    if (gasLimit < std::max(intrinsic_cost, min_cost))
        return make_error_code(ErrorCode::INTRINSIC_GAS_TOO_LOW);

    const auto execution_gas_limit = gasLimit - intrinsic_cost;
    return EthTxProperties{execution_gas_limit, min_cost};
}

/// Map an evmc status code to the FISCO internal TransactionStatus convention
/// (0 = success / None, non-zero = failure).
inline int32_t mapEvmcStatusToBcosStatus(evmc_status_code status)
{
    switch (status)
    {
    case EVMC_SUCCESS:
        return static_cast<int32_t>(protocol::TransactionStatus::None);
    case EVMC_REVERT:
        return static_cast<int32_t>(protocol::TransactionStatus::RevertInstruction);
    case EVMC_OUT_OF_GAS:
        return static_cast<int32_t>(protocol::TransactionStatus::OutOfGas);
    case EVMC_UNDEFINED_INSTRUCTION:
    case EVMC_INVALID_INSTRUCTION:
        return static_cast<int32_t>(protocol::TransactionStatus::BadInstruction);
    case EVMC_BAD_JUMP_DESTINATION:
        return static_cast<int32_t>(protocol::TransactionStatus::BadJumpDestination);
    case EVMC_STACK_OVERFLOW:
        return static_cast<int32_t>(protocol::TransactionStatus::OutOfStack);
    case EVMC_STACK_UNDERFLOW:
        return static_cast<int32_t>(protocol::TransactionStatus::StackUnderflow);
    case EVMC_INSUFFICIENT_BALANCE:
        return static_cast<int32_t>(protocol::TransactionStatus::NotEnoughCash);
    default:
        return static_cast<int32_t>(protocol::TransactionStatus::Unknown);
    }
}

/// Build a BCOS receipt from the executed EVM result (no evmone receipt
/// intermediate). Return data is not retained by the host, matching the v2
/// executor's documented limitation.
template <class Storage>
protocol::TransactionReceipt::Ptr buildBcosReceipt(EthereumHost<Storage>& host,
    evmc::Result const& result, int64_t gasUsed, protocol::TransactionReceiptFactory const& rf,
    int64_t blockNumber)
{
    std::vector<protocol::LogEntry> logs;
    for (auto const& l : host.take_logs())
    {
        bcos::bytes addr(l.addr.bytes, l.addr.bytes + sizeof(evmc_address));
        bcos::h256s topics;
        for (auto const& t : l.topics)
            topics.emplace_back(bcos::bytesConstRef(t.bytes, sizeof(evmc_bytes32)));
        bcos::bytes data(l.data.begin(), l.data.end());
        logs.emplace_back(std::move(addr), std::move(topics), std::move(data));
    }
    bcos::bytes output;
    return rf.createReceipt(bcos::u256(static_cast<uint64_t>(gasUsed)), std::string{}, logs,
        mapEvmcStatusToBcosStatus(result.status_code), bcos::ref(output), blockNumber);
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
template <class Storage>
task::Task<protocol::TransactionReceipt::Ptr> runTransaction(EthereumState<Storage>& state,
    EthBlockInfo const& block, BlockHashLookup blockHashLookup, protocol::Transaction const& tx,
    evmc_revision rev, evmc::VM& vm, EthTxProperties const& txProps, uint64_t chainId,
    EthCallParams const& callParams, protocol::TransactionReceiptFactory const& rf,
    int64_t blockNumber)
{
    const auto gasLimit = effectiveGasLimit(tx, callParams);
    const auto txKind = tx.web3TypedTxKind();
    const auto sender = ethSender(tx);
    const auto to = ethToAddress(tx);

    auto& sender_acc = state.get_or_insert(sender);
    assert(sender_acc.nonce < EthAccount::NonceMax);  // Required for valid tx.
    ++sender_acc.nonce;                               // Bump sender nonce.

    // The NODE's chain id, never tx.chain_id: validate_transaction does not
    // check that field, so passing it would make EIP-7702 step 1 compare sender
    // input against sender input. See the declaration comment in bcos-evm.
    const auto delegation_refund = eth_transition_detail::processAuthorizationList(state, chainId, tx);

    const auto base_fee = (rev >= EVMC_LONDON) ? block.base_fee : 0;
    const auto max_gas_price = ethMaxGasPrice(tx, callParams);
    const auto max_priority_gas_price = ethMaxPriorityGasPrice(tx, callParams);
    assert(max_gas_price >= base_fee);                   // Required for valid tx.
    assert(max_gas_price >= max_priority_gas_price);     // Required for valid tx.
    const auto priority_gas_price =
        std::min(max_priority_gas_price, max_gas_price - base_fee);
    const auto effective_gas_price = base_fee + priority_gas_price;

    assert(effective_gas_price <= max_gas_price);  // Required for valid tx.
    const auto tx_max_cost =
        intx::uint256(static_cast<uint64_t>(gasLimit)) * effective_gas_price;

    sender_acc.balance -= tx_max_cost;  // Modify sender balance after all checks.

    if (txKind == 3)  // blob
    {
        // This uint64 * uint256 cannot overflow, because tx.blob_gas_used has limits enforced
        // before this stage.
        assert(block.blob_base_fee.has_value());
        const auto blob_gas_used =
            static_cast<uint64_t>(evm::GAS_PER_BLOB) * tx.blobVersionedHashes().size();
        const auto blob_fee = intx::umul(intx::uint256(blob_gas_used), *block.blob_base_fee);
        assert(blob_fee <= std::numeric_limits<intx::uint256>::max());
        assert(sender_acc.balance >= blob_fee);  // Required for valid tx.
        sender_acc.balance -= intx::uint256(blob_fee);
    }

    EthereumHost<Storage> host{
        rev, vm, state, block, std::move(blockHashLookup), tx, callParams, chainId};

    sender_acc.access_status = EVMC_ACCESS_WARM;  // Tx sender is always warm.
    if (to.has_value())
        host.access_account(*to);
    for (const auto& entry : tx.web3AccessList())
    {
        evmc::address a{};
        std::copy_n(entry.account.begin(), sizeof(evmc_address), a.bytes);
        host.access_account(a);
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

    auto gas_used = gasLimit - result.gas_left;

    const auto max_refund_quotient = rev >= EVMC_LONDON ? 5 : 2;
    const auto refund_limit = gas_used / max_refund_quotient;
    const auto refund = std::min(delegation_refund + result.gas_refund, refund_limit);
    gas_used -= refund;
    assert(gas_used > 0);

    // EIP-7623: The gas used by the transaction must be at least the min_gas_cost.
    gas_used = std::max(gas_used, txProps.min_gas_cost);

    sender_acc.balance +=
        tx_max_cost - intx::uint256(static_cast<uint64_t>(gas_used)) * effective_gas_price;
    state.touch(block.coinbase).balance +=
        intx::uint256(static_cast<uint64_t>(gas_used)) * priority_gas_price;

    auto receipt = buildBcosReceipt(host, result, gas_used, rf, blockNumber);

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
