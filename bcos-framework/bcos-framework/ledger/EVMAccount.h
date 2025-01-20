#pragma once
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
    std::reference_wrapper<Storage> m_storage;
    std::string m_tableName;

    friend task::Task<bool> tag_invoke(tag_t<exists> /*unused*/, EVMAccount& account)
    {
        co_return co_await storage2::existsOne(account.m_storage.get(),
            transaction_executor::StateKeyView(SYS_TABLES, account.m_tableName));
    }

    friend task::Task<void> tag_invoke(tag_t<create> /*unused*/, EVMAccount& account)
    {
        co_await storage2::writeOne(account.m_storage.get(),
            transaction_executor::StateKey(SYS_TABLES, account.m_tableName),
            storage::Entry{std::string_view{"value"}});
    }

    friend task::Task<std::optional<storage::Entry>> tag_invoke(
        tag_t<code> /*unused*/, EVMAccount& account)
    {
        // 先通过code hash从s_code_binary找代码
        // Start by using the code hash to find the code from the s_code_binary
        if (auto codeHashEntry = co_await storage2::readOne(
                account.m_storage.get(), transaction_executor::StateKeyView{
                                             account.m_tableName, ACCOUNT_TABLE_FIELDS::CODE_HASH}))
        {
            if (auto codeEntry = co_await storage2::readOne(
                    account.m_storage.get(), transaction_executor::StateKeyView{
                                                 ledger::SYS_CODE_BINARY, codeHashEntry->get()}))
            {
                co_return codeEntry;
            }
        }

        // 在s_code_binary里没找到，可能是老版本部署的合约或internal
        // precompiled，代码在合约表的code字段里
        // Can't find it in the s_code_binary, it may be a contract deployed in the old version or
        // internal precompiled, and the code is in the code field of the contract table
        if (auto codeEntry = co_await storage2::readOne(
                account.m_storage.get(), transaction_executor::StateKeyView{
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
        if (!co_await storage2::existsOne(account.m_storage.get(),
                transaction_executor::StateKeyView{ledger::SYS_CODE_BINARY, codeHashEntry.get()}))
        {
            co_await storage2::writeOne(account.m_storage.get(),
                transaction_executor::StateKey{ledger::SYS_CODE_BINARY, codeHashEntry.get()},
                storage::Entry{std::move(code)});
        }

        if (auto codeABI = co_await storage2::readOne(account.m_storage.get(),
                transaction_executor::StateKeyView{ledger::SYS_CONTRACT_ABI, codeHashEntry.get()});
            !codeABI || codeABI->size() == 0)
        {
            co_await storage2::writeOne(account.m_storage.get(),
                transaction_executor::StateKey{ledger::SYS_CONTRACT_ABI, codeHashEntry.get()},
                storage::Entry{std::move(abi)});
        }

        co_await storage2::writeOne(account.m_storage.get(),
            transaction_executor::StateKey{account.m_tableName, ACCOUNT_TABLE_FIELDS::CODE_HASH},
            std::move(codeHashEntry));
    }

    friend task::Task<h256> tag_invoke(tag_t<codeHash> /*unused*/, EVMAccount& account)
    {
        if (auto codeHashEntry = co_await storage2::readOne(
                account.m_storage.get(), transaction_executor::StateKeyView{
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
                account.m_storage.get(), transaction_executor::StateKeyView{
                                             account.m_tableName, ACCOUNT_TABLE_FIELDS::CODE_HASH}))
        {
            if (auto abiEntry = co_await storage2::readOne(
                    account.m_storage.get(), transaction_executor::StateKeyView{
                                                 ledger::SYS_CONTRACT_ABI, codeHashEntry->get()}))
            {
                co_return abiEntry;
            }
        }

        // 在s_code_binary里没找到，可能是老版本部署的合约或internal
        // precompiled，代码在合约表的code字段里
        // I can't find it in the s_code_binary, it may be a contract deployed in the old version or
        // internal precompiled, and the code is in the code field of the contract table
        if (auto abiEntry = co_await storage2::readOne(account.m_storage.get(),
                transaction_executor::StateKeyView{account.m_tableName, ACCOUNT_TABLE_FIELDS::ABI}))
        {
            co_return abiEntry;
        }
        co_return std::optional<storage::Entry>{};
    }

    friend task::Task<u256> tag_invoke(tag_t<balance> /*unused*/, EVMAccount& account)
    {
        if (auto balanceEntry = co_await storage2::readOne(
                account.m_storage.get(), transaction_executor::StateKeyView{
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
        co_await storage2::writeOne(account.m_storage.get(),
            transaction_executor::StateKey{account.m_tableName, ACCOUNT_TABLE_FIELDS::BALANCE},
            std::move(balanceEntry));
    }

    friend task::Task<std::optional<std::string>> tag_invoke(
        tag_t<nonce> /*unused*/, EVMAccount& account)
    {
        if (auto entry = co_await storage2::readOne(
                account.m_storage.get(), transaction_executor::StateKeyView{
                                             account.m_tableName, ACCOUNT_TABLE_FIELDS::NONCE}))
        {
            auto view = entry->get();
            co_return std::string(view);
        }
        co_return std::nullopt;
    }

    friend task::Task<void> tag_invoke(
        tag_t<setNonce> /*unused*/, EVMAccount& account, std::string nonce)
    {
        storage::Entry nonceEntry(std::move(nonce));
        co_await storage2::writeOne(account.m_storage.get(),
            transaction_executor::StateKey{account.m_tableName, ACCOUNT_TABLE_FIELDS::NONCE},
            std::move(nonceEntry));
    }

    friend task::Task<evmc_bytes32> tag_invoke(
        tag_t<storage> /*unused*/, EVMAccount& account, const evmc_bytes32& key)
    {
        evmc_bytes32 value;
        if (auto valueEntry = co_await storage2::readOne(
                account.m_storage.get(), transaction_executor::StateKeyView{account.m_tableName,
                                             concepts::bytebuffer::toView(key.bytes)}))
        {
            auto field = valueEntry->get();
            std::uninitialized_copy_n(field.data(), sizeof(value), value.bytes);
        }
        else
        {
            auto view = std::span(value.bytes);
            std::uninitialized_fill(view.begin(), view.end(), 0);
        }
        co_return value;
    }

    friend task::Task<void> tag_invoke(tag_t<setStorage> /*unused*/, EVMAccount& account,
        const evmc_bytes32& key, const evmc_bytes32& value)
    {
        storage::Entry valueEntry(concepts::bytebuffer::toView(value.bytes));

        co_await storage2::writeOne(account.m_storage.get(),
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
    EVMAccount(EVMAccount&&) noexcept = default;
    EVMAccount& operator=(const EVMAccount&) = delete;
    EVMAccount& operator=(EVMAccount&&) noexcept = default;
    EVMAccount(Storage& storage, const evmc_address& address, bool binaryAddress)
      : m_storage(storage)
    {
        std::array<char, sizeof(address.bytes) * 2> table;  // NOLINT
        boost::algorithm::hex_lower(concepts::bytebuffer::toView(address.bytes), table.data());
        if (auto view = std::string_view(table.data(), table.size());
            precompiled::contains(bcos::precompiled::c_systemTxsAddress, view))
        {
            m_tableName.reserve(ledger::SYS_DIRECTORY::SYS_APPS.size() + table.size());
            m_tableName.append(ledger::SYS_DIRECTORY::SYS_APPS);
            m_tableName.append(std::string_view(table.data(), table.size()));
        }
        else
        {
            if (binaryAddress)
            {
                auto addressView = std::span(address.bytes);
                m_tableName.reserve(ledger::SYS_DIRECTORY::USER_APPS.size() + addressView.size());
                m_tableName.append(ledger::SYS_DIRECTORY::USER_APPS);
                m_tableName.append((const char*)addressView.data(), addressView.size());
            }
            else
            {
                m_tableName.reserve(ledger::SYS_DIRECTORY::USER_APPS.size() + table.size());
                m_tableName.append(ledger::SYS_DIRECTORY::USER_APPS);
                m_tableName.append(std::string_view(table.data(), table.size()));
            }
        }
    }

    /**
     * @brief Construct a new EVMAccount object
     * @param storage storage instance
     * @param address address of the account, hex string, should not contain 0x prefix
     */
    EVMAccount(Storage& storage, std::string_view address, bool binaryAddress) : m_storage(storage)
    {
        if (precompiled::contains(bcos::precompiled::c_systemTxsAddress, address))
        {
            m_tableName.reserve(ledger::SYS_DIRECTORY::SYS_APPS.size() + address.size());
            m_tableName.append(ledger::SYS_DIRECTORY::SYS_APPS);
            m_tableName.append(address);
        }
        else
        {
            if (binaryAddress)
            {
                assert(address.size() % 2 == 0);
                m_tableName.reserve(ledger::SYS_DIRECTORY::USER_APPS.size() + address.size() / 2);
                m_tableName.append(ledger::SYS_DIRECTORY::USER_APPS);
                boost::algorithm::unhex(
                    address.begin(), address.end(), std::back_inserter(m_tableName));
            }
            else
            {
                m_tableName.reserve(ledger::SYS_DIRECTORY::USER_APPS.size() + address.size());
                m_tableName.append(ledger::SYS_DIRECTORY::USER_APPS);
                m_tableName.append(address);
            }
        }
    }
    ~EVMAccount() noexcept = default;

    std::string_view address() const { return this->m_tableName; }
};

template <class Storage>
inline std::ostream& operator<<(std::ostream& stream, EVMAccount<Storage> const& account)
{
    stream << account.address();
    return stream;
}
}  // namespace bcos::ledger::account
