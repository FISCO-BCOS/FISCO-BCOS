#pragma once

#include "bcos-framework/storage2/Storage.h"
#include "bcos-table/src/StateStorage.h"
#include "bcos-task/Trait.h"
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <boost/container/small_vector.hpp>
#include <type_traits>

namespace bcos::transaction_executor
{

template <class Storage>
concept HasReadOneDirect =
    requires(Storage& storage) {
        requires RANGES::range<task::AwaitableReturnType<decltype(storage2::readSome(
            storage, std::declval<std::vector<typename Storage::Key>>(), storage2::READ_FRONT))>>;
    };

template <class Storage>
    requires HasReadOneDirect<Storage>
class Rollbackable
{
private:
    constexpr static size_t MOSTLY_STEPS = 7;
    struct Record
    {
        StateKey key;
        std::optional<StateValue> oldValue;
    };
    boost::container::small_vector<Record, MOSTLY_STEPS> m_records;
    Storage& m_storage;

public:
    using Savepoint = int64_t;
    using Key = typename Storage::Key;
    using Value = typename Storage::Value;

    Rollbackable(Storage& storage) : m_storage(storage) {}

    Storage& storage() { return m_storage; }
    Savepoint current() const { return static_cast<int64_t>(m_records.size()); }

    task::Task<void> rollback(Savepoint savepoint)
    {
        for (auto index = static_cast<int64_t>(m_records.size()); index > savepoint; --index)
        {
            assert(index > 0);
            auto& record = m_records[index - 1];
            if (record.oldValue)
            {
                co_await storage2::writeOne(
                    m_storage, std::move(record.key), std::move(*record.oldValue));
            }
            else
            {
                co_await storage2::removeOne(m_storage, record.key);
            }
            m_records.pop_back();
        }
        co_return;
    }

    friend auto tag_invoke(storage2::tag_t<storage2::readSome> /*unused*/, Rollbackable& storage,
        RANGES::input_range auto&& keys)
        -> task::Task<task::AwaitableReturnType<decltype(storage2::readSome(
            (Storage&)std::declval<Storage>(), std::forward<decltype(keys)>(keys)))>>
    {
        co_return co_await storage2::readSome(
            storage.m_storage, std::forward<decltype(keys)>(keys));
    }

    friend auto tag_invoke(storage2::tag_t<storage2::writeSome> /*unused*/, Rollbackable& storage,
        RANGES::input_range auto&& keys, RANGES::input_range auto&& values)
        -> task::Task<task::AwaitableReturnType<decltype(storage2::writeSome(
            (Storage&)std::declval<Storage>(), std::forward<decltype(keys)>(keys),
            std::forward<decltype(values)>(values)))>>
    {
        // Store values to history
        auto oldValues = co_await storage2::readSome(storage.m_storage, keys, storage2::READ_FRONT);
        for (auto&& [key, value] : RANGES::views::zip(keys, oldValues))
        {
            auto& record =
                storage.m_records.emplace_back(Record{.key = StateKey{key}, .oldValue = {}});
            if (value)
            {
                record.oldValue.emplace(std::move(*value));
            }
        }

        co_return co_await storage2::writeSome(storage.m_storage,
            std::forward<decltype(keys)>(keys), std::forward<decltype(values)>(values));
    }

    friend auto tag_invoke(storage2::tag_t<storage2::removeSome> /*unused*/, Rollbackable& storage,
        RANGES::input_range auto const& keys)
        -> task::Task<task::AwaitableReturnType<decltype(storage2::removeSome(
            (Storage&)std::declval<Storage>(), keys))>>
    {
        // Store values to history
        auto oldValues = co_await storage2::readSome(storage.m_storage, keys, storage2::READ_FRONT);
        for (auto&& [key, value] : RANGES::views::zip(keys, oldValues))
        {
            if (value)
            {
                auto& record = storage.m_records.emplace_back(
                    Record{.key = StateKey{key}, .oldValue = std::move(*value)});
            }
        }

        co_return co_await storage2::removeSome(storage.m_storage, keys);
    }
};

}  // namespace bcos::transaction_executor