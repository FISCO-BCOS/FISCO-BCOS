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
#include <range/v3/view/transform.hpp>

namespace bcos::transaction_executor
{

template <class Storage>
    requires storage2::ReadableStorage<Storage> && storage2::WriteableStorage<Storage> &&
             storage2::ErasableStorage<Storage> && storage2::SeekableStorage<Storage>
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
        }(table, key, std::move(callback)));
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
                auto value = co_await storage2::readSome(
                    self->m_storage, keys | RANGES::views::transform([&table](auto&& key) -> auto& {
                        return StateKeyView{table, key};
                    }));
                callback(nullptr, std::move(value));
            }
            catch (std::exception& e)
            {
                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(-1, "asyncGetRows error!", e), {});
            }
        }(table, std::move(keys), std::move(callback)));
    }

    void asyncSetRow(std::string_view table, std::string_view key, storage::Entry entry,
        std::function<void(Error::UniquePtr)> callback) override
    {
        task::wait([](decltype(this) self, decltype(table) table, decltype(key) key,
                       decltype(entry) entry, decltype(callback) callback) -> task::Task<void> {
            try
            {
                co_await storage2::writeOne(self->m_storage, key, std::move(entry));
                callback(nullptr);
            }
            catch (std::exception& e)
            {
                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(-1, "asyncSetRow error!", e));
            }
        }(table, key, std::move(entry), std::move(callback)));
    }
};

}  // namespace bcos::transaction_executor