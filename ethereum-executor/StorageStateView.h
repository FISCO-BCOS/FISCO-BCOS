/// @file StorageStateView.h
/// @brief An evmone::state::StateView adapter that reads from BCOS MutableStorage
///        via EVMAccount (synchronous bridge using TBB syncWait).
///
/// This adapter allows bcos-evm's transition() engine to read pre-state from
/// the BCOS storage layer without modification.  After execution, the returned
/// StateDiff is written back by the caller.

#pragma once

#include "bcos-evm/eth/state/state.hpp"
#include "bcos-evm/eth/state/state_view.hpp"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-task/TBBWait.h"
#include <evmc/evmc.h>
#include <optional>

namespace bcos::executor_v1::eth
{

/// Helper: convert bcos::u256 to intx::uint256.
/// Defined here to avoid circular dependency with BCOS2Evmone.h.
inline intx::uint256 toIntxU256(bcos::u256 const& val)
{
    auto hexStr = "0x" + val.str(0, std::ios_base::hex);
    return intx::from_string<intx::uint256>(hexStr);
}

/// A read-only StateView backed by BCOS MutableStorage and EVMAccount.
///
/// All three virtual methods are synchronous (required by evmone::state::StateView),
/// so they internally use task::tbb::syncWait to bridge from the async BCOS
/// storage layer. Each override guards the syncWait call in try/catch: the
/// interface is noexcept and syncWait rethrows, so an unguarded exception would
/// terminate the process. A failed read is reported as "absent / empty".
template <class Storage>
class StorageStateView : public evmone::state::StateView
{
public:
    StorageStateView(Storage& storage) : m_storage(storage)
    {
        // Pre-load code cache for precompiled / known addresses?
        // For now, keep it simple — all reads go through EVMAccount.
    }

    std::optional<evmone::state::StateView::Account> get_account(
        const evmc::address& addr) const noexcept override
    {
        // evmone's interface requires noexcept, but the underlying storage
        // reads (via TBB syncWait) can throw (e.g. a malformed nonce/balance
        // entry or a storage backend IO error). syncWait rethrows, so without
        // this guard an exception would cross the noexcept boundary and call
        // std::terminate. Present a failed read as "absent" instead.
        try
        {
            return task::tbb::syncWait(getAccountImpl(addr));
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    evmc::bytes get_account_code(const evmc::address& addr) const noexcept override
    {
        try
        {
            return task::tbb::syncWait(getCodeImpl(addr));
        }
        catch (...)
        {
            return {};
        }
    }

    evmc::bytes32 get_storage(
        const evmc::address& addr, const evmc::bytes32& key) const noexcept override
    {
        try
        {
            return task::tbb::syncWait(getStorageImpl(addr, key));
        }
        catch (...)
        {
            return {};
        }
    }

private:
    Storage& m_storage;

    task::Task<std::optional<evmone::state::StateView::Account>> getAccountImpl(
        evmc_address addr) const
    {
        using namespace bcos::ledger::account;
        auto& storage = const_cast<Storage&>(m_storage);

        EVMAccount<Storage> evmAccount(storage, addr, false);

        if (!co_await evmAccount.exists())
            co_return std::nullopt;

        evmone::state::StateView::Account acc;
        auto nonceVal = co_await evmAccount.nonce();
        if (nonceVal.has_value())
            acc.nonce = static_cast<uint64_t>(bcos::u256(nonceVal.value()));

        acc.balance = toIntxU256(co_await evmAccount.balance());

        auto codeHashVal = co_await evmAccount.codeHash();
        {
            auto const* d = codeHashVal.data();
            bool hasCodeHash = false;
            for (size_t i = 0; i < 32; ++i)
                if (d[i] != 0) { hasCodeHash = true; break; }
            if (hasCodeHash)
                std::copy_n(d, sizeof(evmc_bytes32), acc.code_hash.bytes);
            else
                acc.code_hash = evmone::state::Account::EMPTY_CODE_HASH;
        }

        // Determine whether the account has any non-field storage slots.
        // evmone uses has_storage for EIP-7610 CREATE collision detection and
        // account emptiness. Reporting "true" unconditionally would make every
        // balance-only pre-funded account look like a CREATE collision, which
        // breaks contracts created at pre-funded addresses (EIP-6780 etc.).
        acc.has_storage = co_await hasStorageImpl(evmAccount);
        co_return acc;
    }

    /// Check whether the account table contains any storage slot (a key that
    /// is not one of the fixed account field names).
    task::Task<bool> hasStorageImpl(bcos::ledger::account::EVMAccount<Storage>& evmAccount) const
    {
        using namespace bcos::ledger;
        using namespace bcos::ledger::account;
        auto& storage = const_cast<Storage&>(m_storage);
        auto tableName = co_await evmAccount.path();

        bool hasStorage = false;
        // Seek to the first key of this account's table (empty key sorts first).
        auto it = co_await storage2::range(storage, storage2::RANGE_SEEK,
            executor_v1::StateKey{tableName, std::string_view{}});

        while (auto kv = co_await it.next())
        {
            auto const& [k, v] = *kv;
            executor_v1::StateKeyView view(k);
            if (view.m_table != tableName)
                break;  // Left this account's table.

            auto key = view.m_key;
            if (key != ACCOUNT_TABLE_FIELDS::NONCE &&
                key != ACCOUNT_TABLE_FIELDS::BALANCE &&
                key != ACCOUNT_TABLE_FIELDS::CODE_HASH &&
                key != ACCOUNT_TABLE_FIELDS::CODE &&
                key != ACCOUNT_TABLE_FIELDS::ABI &&
                key != ACCOUNT_TABLE_FIELDS::ALIVE &&
                key != ACCOUNT_TABLE_FIELDS::FROZEN &&
                key != ACCOUNT_TABLE_FIELDS::SHARD)
            {
                hasStorage = true;
                break;
            }
        }
        co_return hasStorage;
    }

    task::Task<evmc::bytes> getCodeImpl(evmc_address addr) const
    {
        using namespace bcos::ledger::account;
        auto& storage = const_cast<Storage&>(m_storage);
        EVMAccount<Storage> evmAccount(storage, addr, false);

        if (!co_await evmAccount.exists())
            co_return {};

        auto codeEntry = co_await evmAccount.code();
        if (!codeEntry.has_value())
            co_return {};

        auto view = codeEntry->get();
        co_return evmc::bytes(view.begin(), view.end());
    }

    task::Task<evmc::bytes32> getStorageImpl(evmc_address addr, evmc_bytes32 key) const
    {
        using namespace bcos::ledger::account;
        auto& storage = const_cast<Storage&>(m_storage);
        EVMAccount<Storage> evmAccount(storage, addr, false);

        // SLOAD on a non-existent account returns 0 per EVM spec.
        // EVMAccount::storage() already returns empty bytes32 for missing
        // accounts/keys, so we can omit the redundant exists() check here
        // and save one storage round-trip per SLOAD.
        co_return co_await evmAccount.storage(key);
    }
};

}  // namespace bcos::executor_v1::eth
