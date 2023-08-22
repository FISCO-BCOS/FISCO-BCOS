#pragma once
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <bcos-task/Trait.h>
#include <oneapi/tbb.h>
#include <compare>
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
    std::unordered_map<typename Storage::Key, ReadWriteFlag> m_readWriteSet;

    void putSet(bool write, auto const& key)
    {
        auto [it, inserted] =
            m_readWriteSet.try_emplace(key, ReadWriteFlag{.read = !write, .write = write});
        if (!inserted)
        {
            it->second.write |= write;
            it->second.read |= (!write);
        }
    }

public:
    using Key = typename Storage::Key;
    using Value = typename Storage::Value;
    ReadWriteSetStorage(Storage& storage) : m_storage(storage) {}

    auto& readWriteSet() { return m_readWriteSet; }
    auto const& readWriteSet() const { return m_readWriteSet; }
    void mergeWriteSet(auto& inputWriteSet)
    {
        auto& writeMap = inputWriteSet.readWriteSet();
        // while (!writeMap.empty())
        // {
        //     auto fromNode = writeMap.extract(writeMap.begin());
        //     if (fromNode.mapped().write)
        //     {
        //         auto result = m_readWriteSet.insert(std::move(fromNode));

        //         if (!result.inserted)
        //         {
        //             result.position->second.write = true;
        //         }
        //     }
        // }

        for (auto& [key, flag] : writeMap)
        {
            if (flag.write)
            {
                putSet(true, key);
            }
        }
    }

    // RAW: read after write
    bool hasRAWIntersection(const auto& rhs) const
    {
        auto const& lhsSet = m_readWriteSet;
        auto const& rhsSet = rhs.readWriteSet();

        if (RANGES::empty(lhsSet) || RANGES::empty(rhsSet))
        {
            return false;
        }

        for (auto const& [key, flag] : rhsSet)
        {
            if (flag.read && lhsSet.contains(key))
            {
                return true;
            }
        }

        return false;
    }

    auto read(RANGES::input_range auto const& keys, bool direct = false)
        -> task::Task<task::AwaitableReturnType<decltype(m_storage.read(keys))>>
    {
        if (!direct)
        {
            for (auto&& key : keys)
            {
                putSet(false, key);
            }
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