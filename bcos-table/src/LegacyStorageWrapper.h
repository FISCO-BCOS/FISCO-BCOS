#pragma once
#include "bcos-framework/storage/Common.h"
#include "bcos-framework/storage/StorageInterface.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-table/src/StateStorageInterface.h"
#include "bcos-task/Task.h"
#include "bcos-task/Wait.h"
#include "bcos-utilities/Error.h"
#include <boost/throw_exception.hpp>
#include <exception>
#include <functional>
#include <stdexcept>

namespace bcos::storage
{

template <class Storage>
class LegacyStorageWrapper : public virtual bcos::storage::StorageInterface
{
private:
    std::reference_wrapper<Storage> m_storage;

public:
    explicit LegacyStorageWrapper(Storage& m_storage) : m_storage(m_storage) {}

    void asyncGetPrimaryKeys(std::string_view table,
        const std::optional<storage::Condition const>& condition,
        std::function<void(Error::UniquePtr, std::vector<std::string>)> _callback) override
    {
        task::wait([](decltype(this) self, std::string table,
                       std::optional<storage::Condition const> condition,
                       decltype(_callback) callback) -> task::Task<void> {
            std::vector<std::string> keys;

            size_t index = 0;
            size_t start = 0;
            size_t count = 0;
            if (condition)
            {
                std::tie(start, count) = condition->getLimit();
            }
            auto range = co_await storage2::range(self->m_storage.get());
            while (auto keyValue = co_await range.next())
            {
                auto&& [key, value] = *keyValue;
                if constexpr (std::is_pointer_v<decltype(value)>)
                {
                    if (!value)
                    {
                        ++index;
                        continue;
                    }
                }

                executor_v1::StateKeyView stateKeyView(key);
                auto [entryTable, entryKey] = stateKeyView.get();
                if (entryTable == table && (!condition || condition->isValid(entryKey)))
                {
                    if (start != 0 || count != 0)
                    {
                        if (((start == 0 || index >= start) &&
                                (count == 0 || index < start + count)))
                        {
                            keys.emplace_back(entryKey);
                            ++index;
                        }
                    }
                    else
                    {
                        keys.emplace_back(entryKey);
                    }
                }
            }

            callback(nullptr, keys);
        }(this, std::string(table), condition, std::move(_callback)));
    }

    void asyncGetRow(std::string_view table, std::string_view key,
        std::function<void(Error::UniquePtr, std::optional<storage::Entry>)> callback) override
    {
        task::wait([](decltype(this) self, decltype(table) table, decltype(key) key,
                       decltype(callback) callback) -> task::Task<void> {
            try
            {
                auto value = co_await storage2::readOne(
                    self->m_storage.get(), executor_v1::StateKeyView{table, key});
                callback(nullptr, std::move(value));
            }
            catch (std::exception& e)
            {
                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(-1, "asyncGetRow error!", e), {});
            }
        }(this, table, key, std::move(callback)));
    }

    void asyncGetRows(std::string_view table,
        ::ranges::any_view<std::string_view, ::ranges::category::input |
                                                 ::ranges::category::random_access |
                                                 ::ranges::category::sized>
            keys,
        std::function<void(Error::UniquePtr, std::vector<std::optional<storage::Entry>>)> callback)
        override
    {
        task::wait([](decltype(this) self, decltype(table) table, decltype(keys) keys,
                       decltype(callback) callback) -> task::Task<void> {
            try
            {
                auto stateKeys = ::ranges::views::transform(keys, [&table](auto&& key) -> auto {
                    return executor_v1::StateKeyView{table, std::forward<decltype(key)>(key)};
                });
                auto values = co_await storage2::readSome(self->m_storage.get(), stateKeys);
                callback(nullptr, std::move(values));
            }
            catch (std::exception& e)
            {
                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(-1, "asyncGetRows error!", e), {});
            }
        }(this, table, std::move(keys), std::move(callback)));
    }

    void asyncSetRow(std::string_view table, std::string_view key, storage::Entry entry,
        std::function<void(Error::UniquePtr)> callback) override
    {
        task::wait([](decltype(this) self, decltype(table) table, decltype(key) key,
                       decltype(entry) entry, decltype(callback) callback) -> task::Task<void> {
            try
            {
                if (entry.status() == storage::Entry::Status::DELETED)
                {
                    co_await storage2::removeOne(
                        self->m_storage.get(), executor_v1::StateKeyView(table, key));
                }
                else
                {
                    co_await storage2::writeOne(
                        self->m_storage.get(), executor_v1::StateKey(table, key), std::move(entry));
                }
                callback(nullptr);
            }
            catch (std::exception& e)
            {
                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(-1, "asyncSetRow error!", e));
            }
        }(this, table, key, std::move(entry), std::move(callback)));
    }

    Error::Ptr setRows(std::string_view tableName,
        ::ranges::any_view<std::string_view,
            ::ranges::category::random_access | ::ranges::category::sized>
            keys,
        ::ranges::any_view<std::string_view,
            ::ranges::category::random_access | ::ranges::category::sized>
            values) override
    {
        return task::syncWait(
            [](decltype(this) self, decltype(tableName) tableName, decltype(keys) keys,
                decltype(values) values) -> task::Task<Error::Ptr> {
                try
                {
                    co_await storage2::writeSome(self->m_storage.get(),
                        ::ranges::views::zip(keys, values) |
                            ::ranges::views::transform([&](auto tuple) {
                                auto& [key, value] = tuple;
                                storage::Entry entry;
                                entry.setField(0, value);
                                return std::make_pair(
                                    executor_v1::StateKey{tableName, key}, std::move(entry));
                            }));
                    co_return nullptr;
                }
                catch (std::exception& e)
                {
                    co_return BCOS_ERROR_WITH_PREV_PTR(-1, "setRows error!", e);
                }
            }(this, tableName, std::move(keys), std::move(values)));
    };
};

template <class Storage>
class LegacyStateStorageWrapper : public virtual storage::StateStorageInterface,
                                  public virtual LegacyStorageWrapper<Storage>
{
public:
    LegacyStateStorageWrapper(Storage& m_storage)
      : StateStorageInterface(nullptr), LegacyStorageWrapper<Storage>(m_storage)
    {}

    void parallelTraverse(bool onlyDirty,
        std::function<bool(const std::string_view& table, const std::string_view& key,
            storage::Entry const& entry)>
            callback) const override
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("Unimplemented!"));
    }

    void rollback(const storage::Recoder& recoder) override
    {
        // 不要抛出异常，会导致TransactionExecutive::revert()后续的transientStateStorage->rollback无法执行
        // Do not throw an exception that will cause Tra::Revert () to follow the Trancintztat
        // Stolag-> Rohrbak not to be executed
        // BOOST_THROW_EXCEPTION(std::runtime_error("Unimplemented!"));
    }

    crypto::HashType hash(
        const bcos::crypto::Hash::Ptr& hashImpl, const ledger::Features& features) const override
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("Unimplemented!"));
    }
};

}  // namespace bcos::storage
