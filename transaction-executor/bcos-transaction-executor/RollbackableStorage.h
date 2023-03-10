#pragma once

#include "bcos-task/Trait.h"
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <boost/container/small_vector.hpp>

namespace bcos::transaction_executor
{

template <StateStorage Storage>
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
                co_await m_storage.write(RANGES::single_view(record.key),
                    RANGES::single_view(std::move(*record.oldValue)));
            }
            else
            {
                co_await m_storage.remove(RANGES::single_view(record.key));
            }
            m_records.pop_back();
        }
        co_return;
    }

    auto read(RANGES::input_range auto const& keys)
        -> task::Task<task::AwaitableReturnType<decltype(m_storage.read(keys))>>
    {
        co_return co_await m_storage.read(keys);
    }

    auto write(RANGES::input_range auto&& keys, RANGES::input_range auto&& values)
        -> task::Task<task::AwaitableReturnType<decltype(m_storage.write(
            std::forward<decltype(keys)>(keys), std::forward<decltype(values)>(values)))>>
    {
        // Store values to history
        {
            auto storageIt = co_await m_storage.read(keys);
            auto keyIt = RANGES::begin(keys);
            while (co_await storageIt.next())
            {
                auto& record = m_records.emplace_back(Record{.key = *(keyIt++), .oldValue = {}});
                if (co_await storageIt.hasValue())
                {
                    // Update exists value, store the old value
                    record.oldValue.emplace(co_await storageIt.value());
                }
            }
        }

        co_return co_await m_storage.write(
            std::forward<decltype(keys)>(keys), std::forward<decltype(values)>(values));
    }

    auto remove(RANGES::input_range auto const& keys)
        -> task::Task<task::AwaitableReturnType<decltype(m_storage.remove(keys))>>
    {
        // Store values to history
        {
            auto storageIt = co_await m_storage.read(keys);
            auto keyIt = RANGES::begin(keys);
            while (co_await storageIt.next())
            {
                auto& record = m_records.emplace_back();
                record.key = *(keyIt++);
                if (co_await storageIt.hasValue())
                {
                    // Update exists value, store the old value
                    record.oldValue.emplace(co_await storageIt.value());
                }
            }
        }

        co_return co_await m_storage.remove(keys);
    }
};

}  // namespace bcos::transaction_executor