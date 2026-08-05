/// @file BCOS2Evmone.h
/// @brief Type converters between BCOS protocol types and evmone state types.
#pragma once

#include "StorageStateView.h"
#include <test/state/state.hpp>
#include <test/state/transaction.hpp>
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/LogEntry.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-task/TBBWait.h"
#include <evmone/evmone.h>
#include <string>
#include <system_error>

namespace bcos::executor_v1::eth
{

using ::bcos::executor_v1::eth::toIntxU256;

// EIP-7840 blob schedule constants (target, max, base_fee_update_fraction).
// Shared by EthereumExecutor (blob_gas_left for validation) and
// blockHeaderToBlockInfo (blob_base_fee computation) so the two cannot drift.
inline constexpr evmone::state::BlobParams PRAGUE_BLOB_PARAMS{
    .target = 6, .max = 9, .base_fee_update_fraction = 5007716};
inline constexpr evmone::state::BlobParams CANCUN_BLOB_PARAMS{
    .target = 3, .max = 6, .base_fee_update_fraction = 3338477};

/// The EIP-7840 blob schedule in effect for @p rev (empty for pre-Cancun).
inline evmone::state::BlobParams blobParamsForRevision(evmc_revision rev) noexcept
{
    if (rev >= EVMC_PRAGUE)
        return PRAGUE_BLOB_PARAMS;  // Prague/Osaka.
    if (rev == EVMC_CANCUN)
        return CANCUN_BLOB_PARAMS;  // Cancun.
    return {};
}

// Non-template converters — defined in BCOS2Evmone.cpp.
evmone::state::BlockInfo blockHeaderToBlockInfo(
    protocol::BlockHeader const& header, ledger::LedgerConfig const& config, evmc_revision rev);

evmone::state::Transaction bcosTransactionToEvmone(protocol::Transaction const& tx);

// Builds a failed BCOS receipt for a transaction that failed evmone's
// validate_transaction (see evmone::state::ErrorCode in test/state errors.hpp).
// Defined in BCOS2Evmone.cpp.
protocol::TransactionReceipt::Ptr validationErrorReceipt(std::error_code const& error,
    protocol::TransactionReceiptFactory const& rf, int64_t blockNumber);

// Defined in BCOS2Evmone.cpp; declared here because the template applyStateDiff
// below (in this header) calls it.
bcos::u256 toBcosU256(intx::uint256 const& val);

/// Remove all storage slots of an account (keeps only the fixed account fields).
/// Used when an account self-destructs: its full state (including storage) must
/// be cleared so that a later CREATE/CREATE2 at the same address is not treated
/// as an EIP-7610 collision.
template <class Storage>
task::Task<void> clearAccountStorage(
    Storage& storage, bcos::ledger::account::EVMAccount<Storage>& acc)
{
    using namespace bcos::ledger;
    using namespace bcos::ledger::account;
    auto tableName = co_await acc.path();

    std::vector<executor_v1::StateKey> keysToRemove;
    auto it = co_await storage2::range(
        storage, storage2::RANGE_SEEK, executor_v1::StateKey{tableName, std::string_view{}});
    while (auto kv = co_await it.next())
    {
        auto const& [k, v] = *kv;
        executor_v1::StateKeyView view(k);
        if (view.m_table != tableName)
            break;  // Left this account's table.
        auto key = view.m_key;
        if (key != ACCOUNT_TABLE_FIELDS::NONCE && key != ACCOUNT_TABLE_FIELDS::BALANCE &&
            key != ACCOUNT_TABLE_FIELDS::CODE_HASH && key != ACCOUNT_TABLE_FIELDS::CODE &&
            key != ACCOUNT_TABLE_FIELDS::ABI && key != ACCOUNT_TABLE_FIELDS::ALIVE &&
            key != ACCOUNT_TABLE_FIELDS::FROZEN && key != ACCOUNT_TABLE_FIELDS::SHARD)
        {
            keysToRemove.emplace_back(k);
        }
    }
    if (!keysToRemove.empty())
        co_await storage2::removeSome(storage, keysToRemove);
}

template <class Storage>
task::Task<void> applyStateDiff(Storage& storage, evmone::state::StateDiff const& diff,
    evmc_revision, crypto::Hash const& hashImpl)
{
    using namespace bcos::ledger::account;

    // Phase 1: Process modified_accounts FIRST.
    // This ensures created accounts exist before we potentially delete them.
    for (auto const& m : diff.modified_accounts)
    {
        EVMAccount<Storage> acc(storage, m.addr, false);
        if (!co_await acc.exists())
            co_await acc.create();
        co_await acc.setNonce(std::to_string(m.nonce));
        co_await acc.setBalance(toBcosU256(m.balance));
        if (m.code.has_value())
        {
            auto const& c = *m.code;
            if (c.empty())
            {
                co_await acc.setCode(bcos::bytes{}, std::string{}, bcos::h256{});
            }
            else
            {
                bcos::bytes code(c.begin(), c.end());
                auto ch = hashImpl.hash(bcos::bytesConstRef(code.data(), code.size()));
                co_await acc.setCode(std::move(code), std::string{}, ch);
            }
        }
        for (auto const& [key, value] : m.modified_storage)
        {
            // NOTE: EVMAccount::setStorage always writes the entry (a zero
            // value leaves a zero-valued row; there is no delete-storage API).
            // Known limitation: such zero rows are only cleaned on self-destruct
            // via clearAccountStorage(). This matches evmone's state semantics
            // (a zero slot reads back as zero) and the sibling zero check in
            // evmoneReceiptToBcos is not required here.
            co_await acc.setStorage(key, value);
        }
    }

    // Phase 2: Process deleted_accounts, but skip addresses that were just
    // created/modified in Phase 1 (recreated accounts).
    // Also track which addresses we processed as deleted.
    for (auto const& addr : diff.deleted_accounts)
    {
        // Check if this address was already handled by modified_accounts above.
        // If so, the account was destructed and recreated in the same tx —
        // the modified state already reflects the recreation, so skip deletion.
        bool inModified = false;
        for (auto const& m : diff.modified_accounts)
        {
            if (memcmp(m.addr.bytes, addr.bytes, sizeof(evmc_address)) == 0)
            {
                inModified = true;
                break;
            }
        }
        if (inModified)
            continue;  // Already handled by modified_accounts processing

        // Genuine deletion: clear account state (including storage, so that a
        // later CREATE/CREATE2 at this address is not an EIP-7610 collision).
        EVMAccount<Storage> acc(storage, addr, false);
        if (co_await acc.exists())
        {
            co_await acc.setBalance(0);
            co_await acc.setNonce("0");
            co_await acc.setCode(bcos::bytes{}, std::string{}, bcos::h256{});
            co_await clearAccountStorage(storage, acc);
        }
    }

    // NOTE: evmone's State::build_diff() may not include recreated accounts
    // (destructed + recreated via CREATE2 in the same tx) in modified_accounts.
    // This is a known limitation — without modifying evmone::state, we cannot
    // recover the lost nonce/balance/code for these accounts.
}

protocol::TransactionReceipt::Ptr evmoneReceiptToBcos(evmone::state::TransactionReceipt const& er,
    protocol::TransactionReceiptFactory const& rf, int64_t blockNumber);

struct ZeroBlockHashes : evmone::state::BlockHashes
{
    evmc::bytes32 get_block_hash(int64_t) const noexcept override { return {}; }
};

}  // namespace bcos::executor_v1::eth
