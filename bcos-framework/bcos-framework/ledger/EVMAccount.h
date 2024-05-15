#pragma once
#include "bcos-executor/src/Common.h"
#include "bcos-framework/executor/PrecompiledTypeDef.h"
#include "bcos-framework/ledger/Account.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-task/Task.h"
#include <evmc/evmc.h>

namespace bcos::ledger::account
{

template <class Storage>
class EVMAccount
{
    // All interface Need block version >= 3.1
private:
    Storage& m_storage;
    static_assert(ledger::SYS_DIRECTORY::USER_APPS.size() > ledger::SYS_DIRECTORY::SYS_APPS.size());
    struct EVMTableName
    {
        std::array<char, ledger::SYS_DIRECTORY::USER_APPS.size()> dir;
        std::array<char, sizeof(evmc_address::bytes) * 2> table;
    } m_tableNameStorage;
    std::string_view m_tableName;

    friend task::Task<void> tag_invoke(tag_t<create> /*unused*/, EVMAccount& account)
    {
        co_await storage2::writeOne(account.m_storage,
            transaction_executor::StateKey(SYS_TABLES, account.m_tableName),
            storage::Entry{std::string_view{"value"}});
    }

    friend task::Task<std::optional<storage::Entry>> tag_invoke(
        tag_t<code> /*unused*/, EVMAccount& account)
    {
        // 先通过code hash从s_code_binary找代码
        // Start by using the code hash to find the code from the s_code_binary
        if (auto codeHashEntry = co_await storage2::readOne(
                account.m_storage, transaction_executor::StateKeyView{
                                       account.m_tableName, ACCOUNT_TABLE_FIELDS::CODE_HASH}))
        {
            if (auto codeEntry = co_await storage2::readOne(
                    account.m_storage, transaction_executor::StateKeyView{
                                           ledger::SYS_CODE_BINARY, codeHashEntry->get()}))
            {
                co_return codeEntry;
            }
        }

        // 在s_code_binary里没找到，可能是老版本部署的合约或internal
        // precompiled，代码在合约表的code字段里
        // I can't find it in the s_code_binary, it may be a contract deployed in the old version or
        // internal precompiled, and the code is in the code field of the contract table
        if (auto codeEntry = co_await storage2::readOne(
                account.m_storage, transaction_executor::StateKeyView{
                                       account.m_tableName, ACCOUNT_TABLE_FIELDS::CODE}))
        {
            co_return codeEntry;
        }
        co_return std::optional<storage::Entry>{};
    }

    friend task::Task<void> tag_invoke(tag_t<setCode> /*unused*/, EVMAccount& account, bytes code,
        std::string abi, const crypto::HashType& codeHash)
    {
        storage::Entry codeHashEntry(concepts::bytebuffer::toView(codeHash));
        if (!co_await storage2::existsOne(account.m_storage,
                transaction_executor::StateKeyView{ledger::SYS_CODE_BINARY, codeHashEntry.get()}))
        {
            co_await storage2::writeOne(account.m_storage,
                transaction_executor::StateKey{ledger::SYS_CODE_BINARY, codeHashEntry.get()},
                storage::Entry{std::move(code)});
        }
        auto codeABI = co_await storage2::readOne(account.m_storage,
            transaction_executor::StateKeyView{ledger::SYS_CONTRACT_ABI, codeHashEntry.get()});
        if (!codeABI || codeABI->size() == 0)
        {
            co_await storage2::writeOne(account.m_storage,
                transaction_executor::StateKey{ledger::SYS_CONTRACT_ABI, codeHashEntry.get()},
                storage::Entry{std::move(abi)});
        }

        co_await storage2::writeOne(account.m_storage,
            transaction_executor::StateKey{account.m_tableName, ACCOUNT_TABLE_FIELDS::CODE_HASH},
            std::move(codeHashEntry));
    }

    friend task::Task<h256> tag_invoke(tag_t<codeHash> /*unused*/, EVMAccount& account)
    {
        if (auto codeHashEntry = co_await storage2::readOne(
                account.m_storage, transaction_executor::StateKeyView{
                                       account.m_tableName, ACCOUNT_TABLE_FIELDS::CODE_HASH}))
        {
            auto view = codeHashEntry->get();
            h256 codeHash((const bcos::byte*)view.data(), view.size());
            co_return codeHash;
        }
        co_return h256{};
    }

    friend task::Task<std::optional<storage::Entry>> tag_invoke(
        tag_t<abi> /*unused*/, EVMAccount& account)
    {
        // 先通过code hash从s_contract_abi找代码
        // Start by using the code hash to find the code from the s_contract_abi
        if (auto codeHashEntry = co_await storage2::readOne(
                account.m_storage, transaction_executor::StateKeyView{
                                       account.m_tableName, ACCOUNT_TABLE_FIELDS::CODE_HASH}))
        {
            if (auto abiEntry = co_await storage2::readOne(
                    account.m_storage, transaction_executor::StateKeyView{
                                           ledger::SYS_CONTRACT_ABI, codeHashEntry->get()}))
            {
                co_return abiEntry;
            }
        }

        // 在s_code_binary里没找到，可能是老版本部署的合约或internal
        // precompiled，代码在合约表的code字段里
        // I can't find it in the s_code_binary, it may be a contract deployed in the old version or
        // internal precompiled, and the code is in the code field of the contract table
        if (auto abiEntry = co_await storage2::readOne(account.m_storage,
                transaction_executor::StateKeyView{account.m_tableName, ACCOUNT_TABLE_FIELDS::ABI}))
        {
            co_return abiEntry;
        }
        co_return std::optional<storage::Entry>{};
    }

    friend task::Task<u256> tag_invoke(tag_t<balance> /*unused*/, EVMAccount& account)
    {
        if (auto balanceEntry = co_await storage2::readOne(
                account.m_storage, transaction_executor::StateKeyView{
                                       account.m_tableName, ACCOUNT_TABLE_FIELDS::BALANCE}))
        {
            auto view = balanceEntry->get();
            auto balance = boost::lexical_cast<u256>(view);
            co_return balance;
        }
        co_return u256{};
    }

    friend task::Task<void> tag_invoke(
        tag_t<setBalance> /*unused*/, EVMAccount& account, const u256& balance)
    {
        storage::Entry balanceEntry(balance.str({}, {}));
        co_await storage2::writeOne(account.m_storage,
            transaction_executor::StateKey{account.m_tableName, ACCOUNT_TABLE_FIELDS::BALANCE},
            std::move(balanceEntry));
    }

    friend task::Task<evmc_bytes32> tag_invoke(
        tag_t<storage> /*unused*/, EVMAccount& account, const evmc_bytes32& key)
    {
        evmc_bytes32 value;
        if (auto valueEntry = co_await storage2::readOne(
                account.m_storage, transaction_executor::StateKeyView{account.m_tableName,
                                       concepts::bytebuffer::toView(key.bytes)}))
        {
            auto field = valueEntry->get();
            std::uninitialized_copy_n(field.data(), sizeof(value), value.bytes);
        }
        else
        {
            std::uninitialized_fill_n(value.bytes, sizeof(value), 0);
        }
        co_return value;
    }

    friend task::Task<void> tag_invoke(tag_t<setStorage> /*unused*/, EVMAccount& account,
        const evmc_bytes32& key, const evmc_bytes32& value)
    {
        storage::Entry valueEntry(concepts::bytebuffer::toView(value.bytes));

        co_await storage2::writeOne(account.m_storage,
            transaction_executor::StateKey{
                account.m_tableName, concepts::bytebuffer::toView(key.bytes)},
            std::move(valueEntry));
    }

    friend task::Task<std::string_view> tag_invoke(tag_t<path> /*unused*/, EVMAccount& account)
    {
        co_return account.m_tableName;
    }

public:
    EVMAccount(const EVMAccount&) = delete;
    EVMAccount(EVMAccount&&) = delete;
    EVMAccount& operator=(const EVMAccount&) = delete;
    EVMAccount& operator=(EVMAccount&&) = delete;
    EVMAccount(Storage& storage, const evmc_address& address) : m_storage(storage)
    {
        constexpr static auto diff =
            ledger::SYS_DIRECTORY::USER_APPS.size() - ledger::SYS_DIRECTORY::SYS_APPS.size();
        boost::algorithm::hex_lower(
            concepts::bytebuffer::toView(address.bytes), m_tableNameStorage.table.data());
        if (auto table =
                std::string_view{m_tableNameStorage.table.data(), m_tableNameStorage.table.size()};
            bcos::precompiled::c_systemTxsAddress.contains(table))
        {
            std::uninitialized_copy(ledger::SYS_DIRECTORY::SYS_APPS.begin(),
                ledger::SYS_DIRECTORY::SYS_APPS.end(), m_tableNameStorage.dir.data() + diff);
            m_tableName = {m_tableNameStorage.dir.data() + diff, sizeof(m_tableNameStorage) - diff};
        }
        else
        {
            std::uninitialized_copy(ledger::SYS_DIRECTORY::USER_APPS.begin(),
                ledger::SYS_DIRECTORY::USER_APPS.end(), m_tableNameStorage.dir.data());
            m_tableName = {m_tableNameStorage.dir.data(), sizeof(m_tableNameStorage)};
        }
    }
    ~EVMAccount() noexcept = default;
};

}  // namespace bcos::ledger::account
