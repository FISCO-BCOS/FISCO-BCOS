#pragma once
#include "bcos-framework/storage/Common.h"
#include "bcos-framework/storage/StorageInterface.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/storage2/StorageMethods.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-task/Task.h"
#include "bcos-task/Wait.h"
#include "bcos-utilities/Error.h"
#include <exception>
#include <iterator>

namespace bcos::transaction_executor
{

template <class Storage>
class StorageWrapper : public bcos::storage::StorageInterface
{
private:
    Storage& m_storage;

public:
    explicit StorageWrapper(Storage& m_storage) : m_storage(m_storage) {}

    void asyncGetPrimaryKeys(std::string_view table,
        const std::optional<storage::Condition const>& condition,
        std::function<void(Error::UniquePtr, std::vector<std::string>)> _callback) override
    {
        _callback(BCOS_ERROR_UNIQUE_PTR(-1, "asyncGetPrimaryKeys error!"), {});
    }

    void asyncGetRow(std::string_view table, std::string_view key,
        std::function<void(Error::UniquePtr, std::optional<storage::Entry>)> callback) override
    {
        task::wait([](decltype(this) self, decltype(table) table, decltype(key) key,
                       decltype(callback) callback) -> task::Task<void> {
            try
            {
                auto value = co_await storage2::readOne(self->m_storage, StateKeyView{table, key});
                callback(nullptr, std::move(value));
            }
            catch (std::exception& e)
            {
                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(-1, "asyncGetRow error!", e), {});
            }
        }(this, table, key, std::move(callback)));
    }

    void asyncGetRows(std::string_view table,
        RANGES::any_view<std::string_view,
            RANGES::category::input | RANGES::category::random_access | RANGES::category::sized>
            keys,
        std::function<void(Error::UniquePtr, std::vector<std::optional<storage::Entry>>)> callback)
        override
    {
        task::wait([](decltype(this) self, decltype(table) table, decltype(keys) keys,
                       decltype(callback) callback) -> task::Task<void> {
            try
            {
                auto stateKeys = keys | RANGES::views::transform([&table](auto&& key) -> auto{
                    return StateKeyView{table, std::forward<decltype(key)>(key)};
                }) | RANGES::to<boost::container::small_vector<StateKeyView, 1>>();
                auto values = co_await storage2::readSome(self->m_storage, stateKeys);

                std::vector<std::optional<storage::Entry>> vectorValues(
                    std::make_move_iterator(values.begin()), std::make_move_iterator(values.end()));
                callback(nullptr, std::move(vectorValues));
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
                co_await storage2::writeOne(
                    self->m_storage, StateKey{table, key}, std::move(entry));
                callback(nullptr);
            }
            catch (std::exception& e)
            {
                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(-1, "asyncSetRow error!", e));
            }
        }(this, table, key, std::move(entry), std::move(callback)));
    }
};

}  // namespace bcos::transaction_executor