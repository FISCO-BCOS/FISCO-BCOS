/// @file OpStackBridge.h
/// @brief Bridge between BCOS storage and bcos-evm's evmone::state::StateView /
///        BlockHashes / StateDiff interfaces, for the OP Stack execution lane.
///
/// The pure-Ethereum lane executes natively against BCOS storage
/// (EthereumState / EthereumTransition — no StateView involved). The OP lane
/// deliberately does NOT re-port the OP transition semantics: opValidate /
/// opTransition / runDeposit in bcos-evm/opstack are the single consensus
/// implementation (fee vault routing, deposit failure semantics, operator-fee
/// conservation), and re-porting them here would create a second copy to keep
/// in sync. Instead this header bridges the executor's coroutine storage to
/// the synchronous StateView those functions consume (reads via
/// task::tbb::syncWait, fail-safe: a failed read reports "absent / empty"),
/// and applies the resulting StateDiff back to BCOS storage.
///
/// Promoted from the EEST runner's test-only bridge (tests/TestStorageBridge.h
/// now forwards here): the OP lane made it production code.

#pragma once

#include "EthereumHost.h"   // BlockHashLookup / EthBlockInfo
#include "EthereumState.h"  // evm::toBcosU256 / evm::keccak256 / eth::clearAccountStorage
#include "bcos-evm/eth/state/state_diff.hpp"
#include "bcos-evm/eth/state/state_view.hpp"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-task/TBBWait.h"
#include <evmc/evmc.h>
#include <atomic>
#include <functional>
#include <optional>

namespace bcos::executor_v1::eth
{

/// A read-only evmone::state::StateView backed by BCOS storage + EVMAccount.
/// Synchronous (required by the StateView interface), bridged with
/// task::tbb::syncWait.
///
/// The noexcept StateView interface cannot propagate a storage failure, so a
/// throwing read answers "absent / empty" to keep execution moving AND latches
/// readFailed() — the executed result was computed against fabricated state.
/// Callers MUST check readFailed() before applying the resulting StateDiff and
/// discard the execution when it is set; treating an I/O failure as "account
/// does not exist / slot is zero" would otherwise persist a wrong receipt and
/// state.
template <class Storage>
class StorageStateView : public evmone::state::StateView
{
public:
    StorageStateView(Storage& storage) : m_storage(storage) {}

    std::optional<evmone::state::StateView::Account> get_account(
        const evmc::address& addr) const noexcept override
    {
        try
        {
            return task::tbb::syncWait(getAccountImpl(addr));
        }
        catch (...)
        {
            m_readFailed.store(true, std::memory_order_relaxed);
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
            m_readFailed.store(true, std::memory_order_relaxed);
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
            m_readFailed.store(true, std::memory_order_relaxed);
            return {};
        }
    }

    /// True when any read above swallowed a storage failure — the answers given
    /// since then are not the committed state.
    [[nodiscard]] bool readFailed() const noexcept
    {
        return m_readFailed.load(std::memory_order_relaxed);
    }

private:
    Storage& m_storage;
    mutable std::atomic_bool m_readFailed{false};

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

        acc.balance = evm::toIntxU256(co_await evmAccount.balance());

        auto codeHashVal = co_await evmAccount.codeHash();
        {
            auto const* d = codeHashVal.data();
            bool hasCodeHash = false;
            for (size_t i = 0; i < 32; ++i)
                if (d[i] != 0)
                {
                    hasCodeHash = true;
                    break;
                }
            if (hasCodeHash)
                std::copy_n(d, sizeof(evmc_bytes32), acc.code_hash.bytes);
            else
                acc.code_hash = EthAccount::EMPTY_CODE_HASH;
        }

        acc.has_storage = co_await hasStorageImpl(evmAccount);
        co_return acc;
    }

    task::Task<bool> hasStorageImpl(bcos::ledger::account::EVMAccount<Storage>& evmAccount) const
    {
        using namespace bcos::ledger;
        using namespace bcos::ledger::account;
        auto& storage = const_cast<Storage&>(m_storage);
        auto tableName = co_await evmAccount.path();

        bool hasStorage = false;
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
        co_return co_await evmAccount.storage(key);
    }
};

/// evmone::state::BlockHashes over the executor's injected BlockHashLookup
/// (BLOCKHASH opcode). An absent lookup — or one that throws — reports the
/// zero hash ("unknown block"), matching the pure-Ethereum lane's fail-safe.
class LookupBlockHashes final : public evmone::state::BlockHashes
{
public:
    LookupBlockHashes(BlockHashLookup const& lookup, int64_t currentHeight) noexcept
      : m_lookup(lookup), m_currentHeight(currentHeight)
    {}

    evmc::bytes32 get_block_hash(int64_t blockNumber) const noexcept override
    {
        if (!m_lookup)
            return {};
        try
        {
            return m_lookup(blockNumber, m_currentHeight);
        }
        catch (...)
        {
            return {};
        }
    }

private:
    BlockHashLookup const& m_lookup;
    int64_t m_currentHeight;
};

/// Write a bcos-evm StateDiff back to BCOS storage. Code hashing is
/// keccak256 (Ethereum consensus hashing), same as EthereumState's
/// applyToStorage — see the "code hash algorithm" note there.
template <class Storage>
task::Task<void> applyEvmoneStateDiff(Storage& storage, evmone::state::StateDiff const& diff)
{
    using namespace bcos::ledger::account;

    // Phase 1: Process modified_accounts FIRST so created accounts exist.
    for (auto const& m : diff.modified_accounts)
    {
        EVMAccount<Storage> acc(storage, m.addr, false);
        if (!co_await acc.exists())
            co_await acc.create();
        co_await acc.setNonce(std::to_string(m.nonce));
        co_await acc.setBalance(evm::toBcosU256(m.balance));
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
                const auto ch = evm::keccak256({code.data(), code.size()});
                co_await acc.setCode(std::move(code), std::string{},
                    bcos::h256{bcos::bytesConstRef(ch.bytes, sizeof(ch.bytes))});
            }
        }
        for (auto const& [key, value] : m.modified_storage)
            co_await acc.setStorage(key, value);
    }

    // Phase 2: deleted accounts, skipping addresses recreated in Phase 1.
    for (auto const& addr : diff.deleted_accounts)
    {
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
            continue;

        EVMAccount<Storage> acc(storage, addr, false);
        if (co_await acc.exists())
        {
            co_await acc.setBalance(0);
            co_await acc.setNonce("0");
            co_await acc.setCode(bcos::bytes{}, std::string{}, bcos::h256{});
            co_await clearAccountStorage(storage, acc);
        }
    }
}

}  // namespace bcos::executor_v1::eth
