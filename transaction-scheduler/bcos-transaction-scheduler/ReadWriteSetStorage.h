#pragma once
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <bcos-task/Trait.h>
#include <type_traits>
#include <variant>

namespace bcos::transaction_scheduler
{

template <transaction_executor::StateStorage Storage, bool enableReadSet = true,
    bool enableWriteSet = true>
class ReadWriteSetStorage
{
private:
    using Empty = std::monostate;

    Storage& m_storage;
    [[no_unique_address]] std::conditional_t<enableReadSet,
        std::set<typename Storage::Key, std::less<>>, Empty>
        m_readSet;
    [[no_unique_address]] std::conditional_t<enableWriteSet,
        std::set<typename Storage::Key, std::less<>>, Empty>
        m_writeSet;

    void putSet(decltype(m_readSet)& set, auto const& key)
    {
        auto it = set.lower_bound(key);
        if (it == set.end() || *it != key)
        {
            set.emplace_hint(it, key);
        }
    }

public:
    using Key = typename Storage::Key;
    using Value = typename Storage::Value;
    ReadWriteSetStorage(Storage& storage) : m_storage(storage) {}

    // RAW means read after write
    bool hasRAWIntersection(const ReadWriteSetStorage& rhs)
    {
        auto const& lhsSet = m_writeSet;
        auto const& rhsSet = rhs.m_readSet;

        if (RANGES::empty(lhsSet) || RANGES::empty(rhsSet))
        {
            return false;
        }

        if ((RANGES::back(lhsSet) < RANGES::front(rhsSet)) ||
            (RANGES::front(lhsSet) > RANGES::back(rhsSet)))
        {
            return false;
        }

        auto lBegin = RANGES::begin(lhsSet);
        auto lEnd = RANGES::end(lhsSet);
        auto rBegin = RANGES::begin(rhsSet);
        auto rEnd = RANGES::end(rhsSet);

        // O(lhsSet.size() + rhsSet.size())
        while (lBegin != lEnd && rBegin != rEnd)
        {
            if (*lBegin < *rBegin)
            {
                RANGES::advance(lBegin, 1);
            }
            else
            {
                if (!(*rBegin < *lBegin))
                {
                    return true;
                }
                RANGES::advance(rBegin, 1);
            }
        }

        return false;
    }

    auto read(RANGES::input_range auto const& keys)
        -> task::Task<task::AwaitableReturnType<decltype(m_storage.read(keys))>>
    {
        if constexpr (enableReadSet)
        {
            for (auto&& key : keys)
            {
                putSet(m_readSet, key);
            }
        }
        co_return co_await m_storage.read(keys);
    }

    auto write(RANGES::input_range auto&& keys, RANGES::input_range auto&& values)
        -> task::Task<task::AwaitableReturnType<decltype(m_storage.write(
            std::forward<decltype(keys)>(keys), std::forward<decltype(values)>(values)))>>
    {
        if constexpr (enableWriteSet)
        {
            for (auto&& key : keys)
            {
                putSet(m_writeSet, std::forward<decltype(key)>(key));
            }
        }
        co_return co_await m_storage.write(
            std::forward<decltype(keys)>(keys), std::forward<decltype(values)>(values));
    }

    auto remove(RANGES::input_range auto const& keys)
        -> task::Task<task::AwaitableReturnType<decltype(m_storage.remove(keys))>>
    {
        if constexpr (enableWriteSet)
        {
            for (auto&& key : keys)
            {
                putSet(m_writeSet, std::forward<decltype(key)>(key));
            }
        }
        co_return co_await m_storage.remove(keys);
    }
};

}  // namespace bcos::transaction_scheduler