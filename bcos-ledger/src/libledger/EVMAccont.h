#pragma once
#include "bcos-concepts/ByteBuffer.h"
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
    constexpr static auto EVM_TABLE_NAME_LENGTH =
        ledger::SYS_DIRECTORY::USER_APPS.size() + sizeof(evmc_address::bytes) * 2;
    using EVMTableName = std::array<char, EVM_TABLE_NAME_LENGTH>;

    Storage& m_storage;
    EVMTableName m_tableName;

    friend task::Task<void> tag_invoke(tag_t<create> /*unused*/, EVMAccount& account)
    {
        co_await storage2::writeOne(account.m_storage,
            transaction_executor::StateKey(
                SYS_TABLES, concepts::bytebuffer::toView(account.m_tableName)),
            storage::Entry{std::string_view{"value"}});
    }

    friend task::Task<std::optional<storage::Entry>> tag_invoke(
        tag_t<code> /*unused*/, EVMAccount& account)
    {
        auto codeHashEntry = co_await storage2::readOne(account.m_storage,
            transaction_executor::StateKeyView{concepts::bytebuffer::toView(account.m_tableName),
                ACCOUNT_TABLE_FIELDS::CODE_HASH});
        if (codeHashEntry)
        {
            auto codeEntry = co_await storage2::readOne(account.m_storage,
                transaction_executor::StateKeyView{ledger::SYS_CODE_BINARY, codeHashEntry->get()});
            if (codeEntry)
            {
                co_return codeEntry;
            }
        }
        co_return std::optional<storage::Entry>{};
    }

    friend task::Task<void> tag_invoke(tag_t<setCode> /*unused*/, EVMAccount& account, bytes code,
        const crypto::HashType& codeHash)
    {
        storage::Entry codeHashEntry;
        codeHashEntry.set(concepts::bytebuffer::toView(codeHash));

        // Query the code table first
        auto existsCode = co_await storage2::readOne(account.m_storage,
            transaction_executor::StateKeyView{ledger::SYS_CODE_BINARY, codeHashEntry.get()});
        if (!existsCode)
        {
            storage::Entry codeEntry(std::move(code));
            co_await storage2::writeOne(account.m_storage,
                transaction_executor::StateKey{ledger::SYS_CODE_BINARY, codeHashEntry.get()},
                std::move(codeEntry));
        }
        co_await storage2::writeOne(account.m_storage,
            transaction_executor::StateKey{
                concepts::bytebuffer::toView(account.m_tableName), ACCOUNT_TABLE_FIELDS::CODE_HASH},
            std::move(codeHashEntry));
    }

    friend task::Task<void> tag_invoke(
        tag_t<setCode> /*unused*/, EVMAccount& account, bytes code, const crypto::Hash& hashImpl)
    {
        auto codeHash = hashImpl.hash(code);
        co_await account::setCode(account, std::move(code), codeHash);
    }

    friend task::Task<h256> tag_invoke(tag_t<codeHash> /*unused*/, EVMAccount& account)
    {
        auto codeHashEntry = co_await storage2::readOne(account.m_storage,
            transaction_executor::StateKeyView{concepts::bytebuffer::toView(account.m_tableName),
                ACCOUNT_TABLE_FIELDS::CODE_HASH});
        if (codeHashEntry)
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
        co_return co_await storage2::readOne(account.m_storage,
            transaction_executor::StateKeyView{
                concepts::bytebuffer::toView(account.m_tableName), ACCOUNT_TABLE_FIELDS::ABI});
    }

    friend task::Task<u256> tag_invoke(tag_t<balance> /*unused*/, EVMAccount& account)
    {
        auto balanceEntry = co_await storage2::readOne(account.m_storage,
            transaction_executor::StateKeyView{
                concepts::bytebuffer::toView(account.m_tableName), ACCOUNT_TABLE_FIELDS::BALANCE});

        if (balanceEntry)
        {
            auto view = balanceEntry->get();
            auto balance = boost::lexical_cast<u256>(view);
            co_return balance;
        }
        co_return u256{};
    }

    friend task::Task<u256> tag_invoke(
        tag_t<setBalance> /*unused*/, EVMAccount& account, const u256& balance)
    {
        storage::Entry balanceEntry(boost::lexical_cast<std::string>(balance));
        co_await storage2::writeOne(account.m_storage,
            transaction_executor::StateKey{
                concepts::bytebuffer::toView(account.m_tableName), ACCOUNT_TABLE_FIELDS::BALANCE},
            std::move(balanceEntry));
    }

    friend task::Task<evmc_bytes32> tag_invoke(
        tag_t<storage> /*unused*/, EVMAccount& account, const evmc_bytes32& key)
    {
        auto valueEntry = co_await storage2::readOne(account.m_storage,
            transaction_executor::StateKeyView{concepts::bytebuffer::toView(account.m_tableName),
                concepts::bytebuffer::toView(key.bytes)});

        evmc_bytes32 value;
        if (valueEntry)
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
            transaction_executor::StateKey{concepts::bytebuffer::toView(account.m_tableName),
                concepts::bytebuffer::toView(key.bytes)},
            std::move(valueEntry));
    }

public:
    EVMAccount(const EVMAccount&) = delete;
    EVMAccount(EVMAccount&&) = delete;
    EVMAccount& operator=(const EVMAccount&) = delete;
    EVMAccount& operator=(EVMAccount&&) = delete;
    EVMAccount(Storage& storage, const evmc_address& address) : m_storage(storage)
    {
        auto* lastIt = std::uninitialized_copy(ledger::SYS_DIRECTORY::USER_APPS.begin(),
            ledger::SYS_DIRECTORY::USER_APPS.end(), m_tableName.data());
        boost::algorithm::hex_lower(concepts::bytebuffer::toView(address.bytes), lastIt);
    }
    ~EVMAccount() noexcept = default;
};

}  // namespace bcos::ledger::account