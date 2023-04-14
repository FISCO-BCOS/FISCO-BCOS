#pragma once

#include "bcos-task/Trait.h"
#include <bcos-framework/transaction-executor/TransactionExecutor.h>

namespace bcos::transaction_executor
{

// Rollbackable crtp base
template <storage2::Storage<StateKey, StateValue> Storage>
class Rollbackable : public Storage
{
private:
    struct Record
    {
        StateKey key;
        std::optional<StateValue> value;
    };
    std::forward_list<Record> m_records;

public:
    using Savepoint = typename std::forward_list<Record>::const_iterator;
    using Storage::Storage;

    Savepoint current() const { return m_records.cbegin(); }
    task::Task<void> rollback(Savepoint savepoint)
    {
        auto it = m_records.begin();
        while (it != savepoint)
        {
            auto& record = *it;
            if (record.value)
            {
                co_await Storage::write(
                    storage2::single(record.key), storage2::single(*record.value));
            }
            else
            {
                co_await Storage::remove(storage2::single(record.key));
            }
            m_records.pop_front();
            it = m_records.begin();
        }
        co_return;
    }

    auto write(RANGES::input_range auto&& keys, RANGES::input_range auto&& values)
        -> task::Task<task::AwaitableReturnType<decltype(Storage::write(
            std::forward<decltype(keys)>(keys), std::forward<decltype(values)>(values)))>>
    {
        // Store values to history
        {
            auto storageIt = co_await Storage::read(keys);
            auto keyIt = RANGES::begin(keys);
            while (co_await storageIt.next())
            {
                auto& record = m_records.emplace_front();
                record.key = *(keyIt++);
                if (co_await storageIt.hasValue())
                {
                    // Update exists value, store the old value
                    record.value = {co_await storageIt.value()};
                }
                else
                {
                    record.value = {};
                }
            }
        }

        co_return co_await Storage::write(
            std::forward<decltype(keys)>(keys), std::forward<decltype(values)>(values));
    }

    auto remove(RANGES::input_range auto const& keys)
        -> task::Task<task::AwaitableReturnType<decltype(Storage::remove(keys))>>
    {
        // Store values to history
        {
            auto storageIt = co_await Storage::read(keys);
            auto keyIt = RANGES::begin(keys);
            while (co_await storageIt.next())
            {
                auto& record = m_records.emplace_front();
                record.key = *(keyIt++);
                if (co_await storageIt.hasValue())
                {
                    // Update exists value, store the old value
                    record.value = {co_await storageIt.value()};
                }
            }
        }

        co_return co_await Storage::remove(keys);
    }
};

}  // namespace bcos::transaction_executor