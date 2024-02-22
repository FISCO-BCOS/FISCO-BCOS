#pragma once

#include "bcos-framework/storage2/Storage.h"
#include "bcos-table/src/StateStorage.h"
#include "bcos-task/Trait.h"
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <type_traits>

namespace bcos::transaction_executor
{

template <class Storage>
concept HasReadOneDirect =
    requires(Storage& storage) {
        requires !std::is_void_v<task::AwaitableReturnType<decltype(storage2::readOne(
            storage, std::declval<typename Storage::Key>(), storage2::READ_FRONT))>>;
    };
template <class Storage>
concept HasReadSomeDirect =
    requires(Storage& storage) {
        requires RANGES::range<task::AwaitableReturnType<decltype(storage2::readSome(
            storage, std::declval<std::vector<typename Storage::Key>>(), storage2::READ_FRONT))>>;
    };

template <class Storage>
class Rollbackable
{
private:
    struct Record
    {
        StateKey key;
        task::AwaitableReturnType<std::invoke_result_t<decltype(storage2::readOne), Storage&,
            typename Storage::Key, storage2::READ_FRONT_TYPE>>
            oldValue;
    };
    std::vector<Record> m_records;
    Storage* m_storage;

public:
    using Savepoint = int64_t;
    using Key = typename Storage::Key;
    using Value = typename Storage::Value;

    Rollbackable(Storage& storage) : m_storage(std::addressof(storage)) {}

    Storage& storage() { return *m_storage; }
    Savepoint current() const { return static_cast<int64_t>(m_records.size()); }

    task::Task<void> rollback(Savepoint savepoint)
    {
        if (m_records.empty())
        {
            co_return;
        }
        for (auto index = static_cast<int64_t>(m_records.size()); index > savepoint; --index)
        {
            assert(index > 0);
            auto& record = m_records[index - 1];
            if (record.oldValue)
            {
                co_await storage2::writeOne(
                    *m_storage, std::move(record.key), std::move(*record.oldValue));
            }
            else
            {
                co_await storage2::removeOne(*m_storage, record.key);
            }
            m_records.pop_back();
        }
        co_return;
    }

    friend auto tag_invoke(storage2::tag_t<storage2::readSome> /*unused*/, Rollbackable& storage,
        RANGES::input_range auto&& keys)
        -> task::Task<task::AwaitableReturnType<
            std::invoke_result_t<storage2::ReadSome, Storage&, decltype(keys)>>>
    {
        co_return co_await storage2::readSome(
            *storage.m_storage, std::forward<decltype(keys)>(keys));
    }

    friend auto tag_invoke(
        storage2::tag_t<storage2::readOne> /*unused*/, Rollbackable& storage, auto&& key)
        -> task::Task<task::AwaitableReturnType<
            std::invoke_result_t<storage2::ReadOne, Storage&, decltype(key)>>>
    {
        co_return co_await storage2::readOne(*storage.m_storage, std::forward<decltype(key)>(key));
    }

    friend auto tag_invoke(storage2::tag_t<storage2::writeSome> /*unused*/, Rollbackable& storage,
        RANGES::input_range auto&& keys, RANGES::input_range auto&& values)
        -> task::Task<task::AwaitableReturnType<
            std::invoke_result_t<storage2::WriteSome, Storage&, decltype(keys), decltype(values)>>>
        requires HasReadSomeDirect<Storage>
    {
        auto oldValues =
            co_await storage2::readSome(*storage.m_storage, keys, storage2::READ_FRONT);
        for (auto&& [key, oldValue] : RANGES::views::zip(keys, oldValues))
        {
            storage.m_records.emplace_back(Record{.key = typename Storage::Key{key},
                .oldValue = std::forward<decltype(oldValue)>(oldValue)});
        }

        co_return co_await storage2::writeSome(*storage.m_storage,
            std::forward<decltype(keys)>(keys), std::forward<decltype(values)>(values));
    }

    friend auto tag_invoke(storage2::tag_t<storage2::writeOne> /*unused*/, Rollbackable& storage,
        auto&& key, auto&& value)
        -> task::Task<task::AwaitableReturnType<
            std::invoke_result_t<storage2::WriteOne, Storage&, decltype(key), decltype(value)>>>
        requires HasReadOneDirect<Storage>
    {
        auto& record = storage.m_records.emplace_back();
        record.key = key;
        record.oldValue = co_await storage2::readOne(*storage.m_storage, key, storage2::READ_FRONT);
        co_await storage2::writeOne(
            *storage.m_storage, record.key, std::forward<decltype(value)>(value));
    }

    friend auto tag_invoke(storage2::tag_t<storage2::removeSome> /*unused*/, Rollbackable& storage,
        RANGES::input_range auto const& keys)
        -> task::Task<task::AwaitableReturnType<
            std::invoke_result_t<storage2::RemoveSome, Storage&, decltype(keys)>>>
    {
        // Store values to history
        auto oldValues =
            co_await storage2::readSome(*storage.m_storage, keys, storage2::READ_FRONT);
        for (auto&& [key, value] : RANGES::views::zip(keys, oldValues))
        {
            if (value)
            {
                auto& record = storage.m_records.emplace_back(
                    Record{.key = StateKey{key}, .oldValue = std::move(*value)});
            }
        }

        co_return co_await storage2::removeSome(*storage.m_storage, keys);
    }
};

}  // namespace bcos::transaction_executor