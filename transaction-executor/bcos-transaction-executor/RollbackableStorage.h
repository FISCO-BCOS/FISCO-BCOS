#pragma once

#include "bcos-task/Trait.h"
#include <bcos-framework/transaction-executor/Concepts.h>
#include <bcos-framework/transaction-executor/TransactionExecutor.h>

namespace bcos::transaction_executor
{

// Rollbackable crtp base
template <StateStorage Storage>
class Rollbackable
{
private:
    struct Record
    {
        StateKey key;
        std::optional<StateValue> value;
    };
    std::forward_list<Record> m_records;
    Storage& m_storage;

public:
    using Savepoint = typename std::forward_list<Record>::const_iterator;

    Rollbackable(Storage& storage) : m_storage(storage) {}

    Savepoint current() const { return m_records.cbegin(); }
    task::Task<void> rollback(Savepoint savepoint)
    {
        auto it = m_records.begin();
        while (it != savepoint)
        {
            auto& record = *it;
            if (record.value)
            {
                co_await m_storage.write(
                    storage2::single(record.key), storage2::single(*record.value));
            }
            else
            {
                co_await m_storage.remove(storage2::single(record.key));
            }
            m_records.pop_front();
            it = m_records.begin();
        }
        co_return;
    }

    auto read(RANGES::input_range auto const& keys)
        -> task::Task<task::AwaitableReturnType<decltype(m_storage.read(keys))>>
    {
        co_return co_await m_storage.read(keys);
    }

    auto seek(auto const& key)
        -> task::Task<task::AwaitableReturnType<decltype(m_storage.seek(key))>>
    {
        co_return co_await m_storage.seek(key);
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
                auto& record = m_records.emplace_front(Record{.key = *(keyIt++), .value = {}});
                if (co_await storageIt.hasValue())
                {
                    // Update exists value, store the old value
                    record.value.emplace(co_await storageIt.value());
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
                auto& record = m_records.emplace_front();
                record.key = *(keyIt++);
                if (co_await storageIt.hasValue())
                {
                    // Update exists value, store the old value
                    record.value.emplace(co_await storageIt.value());
                }
            }
        }

        co_return co_await m_storage.remove(keys);
    }
};

}  // namespace bcos::transaction_executor