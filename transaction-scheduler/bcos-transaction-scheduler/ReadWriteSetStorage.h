#pragma once
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <bcos-task/Trait.h>
#include <type_traits>
#include <variant>

namespace bcos::transaction_scheduler
{

template <transaction_executor::StateStorage Storage>
class ReadWriteSetStorage
{
private:
    Storage& m_storage;
    struct ReadWriteFlag
    {
        bool read = false;
        bool write = false;
    };
    std::map<typename Storage::Key, ReadWriteFlag, std::less<>> m_readWriteSet;
    void putSet(bool write, auto const& key)
    {
        auto it = m_readWriteSet.lower_bound(key);
        if (it == m_readWriteSet.end() || it->first != key)
        {
            it = m_readWriteSet.emplace_hint(it, key, ReadWriteFlag{});
        }

        if (write)
        {
            it->second.write = true;
        }
        else
        {
            it->second.read = true;
        }
    }

public:
    using Key = typename Storage::Key;
    using Value = typename Storage::Value;
    ReadWriteSetStorage(Storage& storage) : m_storage(storage) {}

    // RAW means read after write
    bool hasRAWIntersection(const ReadWriteSetStorage& rhs)
    {
        auto const& lhsSet = m_readWriteSet;
        auto const& rhsSet = rhs.m_readWriteSet;

        if (RANGES::empty(lhsSet) || RANGES::empty(rhsSet))
        {
            return false;
        }

        if ((RANGES::back(lhsSet).first < RANGES::front(rhsSet).first) ||
            (RANGES::front(lhsSet).first > RANGES::back(rhsSet).first))
        {
            return false;
        }

        auto lhsRange = lhsSet |
                        RANGES::views::filter([](auto& pair) { return pair.second.write; }) |
                        RANGES::views::keys;
        auto rhsRange = rhsSet |
                        RANGES::views::filter([](auto& pair) { return pair.second.read; }) |
                        RANGES::views::keys;

        auto lBegin = RANGES::begin(lhsRange);
        auto lEnd = RANGES::end(lhsRange);
        auto rBegin = RANGES::begin(rhsRange);
        auto rEnd = RANGES::end(rhsRange);

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
        for (auto&& key : keys)
        {
            putSet(false, key);
        }
        co_return co_await m_storage.read(keys);
    }

    auto write(RANGES::input_range auto&& keys, RANGES::input_range auto&& values)
        -> task::Task<task::AwaitableReturnType<decltype(m_storage.write(
            std::forward<decltype(keys)>(keys), std::forward<decltype(values)>(values)))>>
    {
        for (auto&& key : keys)
        {
            putSet(true, std::forward<decltype(key)>(key));
        }
        co_return co_await m_storage.write(
            std::forward<decltype(keys)>(keys), std::forward<decltype(values)>(values));
    }

    auto remove(RANGES::input_range auto const& keys)
        -> task::Task<task::AwaitableReturnType<decltype(m_storage.remove(keys))>>
    {
        for (auto&& key : keys)
        {
            putSet(true, std::forward<decltype(key)>(key));
        }
        co_return co_await m_storage.remove(keys);
    }
};

}  // namespace bcos::transaction_scheduler