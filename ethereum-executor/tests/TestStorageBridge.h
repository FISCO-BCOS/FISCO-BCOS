/// @file TestStorageBridge.h
/// @brief TEST-ONLY bridge between BCOS storage and bcos-evm's
///        evmone::state::StateView / StateDiff interfaces.
///
/// The ethereum-executor LIBRARY no longer ships the StateView / StateDiff
/// adapter layer (StorageStateView / StorageBlockHashes / applyStateDiff were
/// removed — the executor reads/writes BCOS storage directly through
/// EVMAccount / storage2). However the EEST runner also drives bcos-evm's
/// system contracts (EIP-4788 / EIP-2935 / EIP-7002 / EIP-7251) directly, and
/// those keep the upstream evmone::state::{StateView, BlockHashes} interface
/// (bcos-evm is unchanged). This header is the small local bridge the test tool
/// needs for that path only; it is NOT part of the executor library.

#pragma once

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-evm/eth/state/account.hpp"
#include "bcos-evm/eth/state/state_diff.hpp"
#include "bcos-evm/eth/state/state_view.hpp"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-task/TBBWait.h"
#include "ethereum-executor/EthereumState.h"  // eth::EthAccount / eth::clearAccountStorage
#include <evmc/evmc.h>
#include <optional>

namespace bcos::test
{

// bcos::u256 <-> intx::uint256 for the bcos-evm oracle types this bridge drives (the
// evmone::state interfaces stay intx). ethereum-executor's own arithmetic and interfaces no
// longer use intx, so these test-local copies retire together with the bcos-evm dependency.
inline intx::uint256 testToIntxU256(bcos::u256 const& val)
{
    std::array<bcos::byte, 32> be{};
    bcos::toBigEndian(val, be);
    return intx::be::unsafe::load<intx::uint256>(be.data());
}
inline bcos::u256 testToBcosU256(intx::uint256 const& val)
{
    const auto be = intx::be::store<evmc::bytes32>(val);
    return bcos::fromBigEndian<bcos::u256>(bcos::bytesConstRef(be.bytes, sizeof(be.bytes)));
}

/// A read-only evmone::state::StateView backed by BCOS storage + EVMAccount.
/// Synchronous (required by the StateView interface), bridged with
/// task::tbb::syncWait, fail-safe (a failed read reports "absent / empty").
template <class Storage>
class TestStorageStateView : public evmone::state::StateView
{
public:
    TestStorageStateView(Storage& storage) : m_storage(storage) {}

    std::optional<evmone::state::StateView::Account> get_account(
        const evmc::address& addr) const noexcept override
    {
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

        acc.balance = testToIntxU256(co_await evmAccount.balance());

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
                acc.code_hash = evmone::state::Account::EMPTY_CODE_HASH;
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

/// TEST-ONLY: write a bcos-evm StateDiff back to BCOS storage. The executor
/// library no longer has an applyStateDiff (it writes BCOS storage directly);
/// this helper exists only so the EEST runner can persist the state diff of
/// bcos-evm's system_call_block_start/end system contracts.
template <class Storage>
task::Task<void> testApplyStateDiff(
    Storage& storage, evmone::state::StateDiff const& diff, crypto::Hash const& hashImpl)
{
    using namespace bcos::ledger::account;
    using bcos::executor_v1::eth::clearAccountStorage;

    // Phase 1: Process modified_accounts FIRST so created accounts exist.
    for (auto const& m : diff.modified_accounts)
    {
        EVMAccount<Storage> acc(storage, m.addr, false);
        if (!co_await acc.exists())
            co_await acc.create();
        co_await acc.setNonce(std::to_string(m.nonce));
        co_await acc.setBalance(testToBcosU256(m.balance));
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

}  // namespace bcos::test
