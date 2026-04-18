#pragma once
#include "bcos-framework/storage2/Storage.h"
#include <bcos-task/Trait.h>
#include <type_traits>
#include <utility>

namespace bcos::scheduler_v1
{

template <class StorageType>
class ReadWriteSetStorage
{
private:
    std::reference_wrapper<std::remove_reference_t<StorageType>> m_storage;

public:
    using Key = std::decay_t<StorageType>::Key;
    using Value = std::decay_t<StorageType>::Value;
    ReadWriteSetStorage(StorageType& storage) : m_storage(std::ref(storage)) {}

private:
    struct ReadWriteFlag
    {
        bool read = false;
        bool write = false;
    };
    std::unordered_map<size_t, ReadWriteFlag> m_readWriteSet;
    using Storage = StorageType;

    void putSet(bool write, size_t hash)
    {
        auto [it, inserted] = m_readWriteSet.try_emplace(
            hash, typename ReadWriteSetStorage::ReadWriteFlag{.read = !write, .write = write});
        if (!inserted)
        {
            it->second.write |= write;
            it->second.read |= (!write);
        }
    }
    void putSet(bool write, auto const& key)
    {
        auto hash = std::hash<Key>{}(key);
        putSet(write, hash);
    }

public:

    auto readSome(::ranges::input_range auto keys)
        -> task::Task<task::AwaitableReturnType<
            std::invoke_result_t<storage2::ReadSome, Storage&, decltype(keys)>>>
    {
        for (auto&& key : keys)
        {
            putSet(false, key);
        }
        co_return co_await storage2::readSome(m_storage.get(), std::move(keys));
    }

    auto readSome(::ranges::input_range auto keys, storage2::DIRECT_TYPE direct)
        -> task::Task<task::AwaitableReturnType<std::invoke_result_t<storage2::ReadSome,
            std::add_lvalue_reference_t<Storage>, decltype(std::move(keys))>>>
    {
        co_return co_await storage2::readSome(m_storage.get(), std::move(keys), direct);
    }

    auto readOne(auto key)
        -> task::Task<task::AwaitableReturnType<std::invoke_result_t<storage2::ReadOne,
            std::add_lvalue_reference_t<Storage>, decltype(key)>>>
    {
        putSet(false, key);
        co_return co_await storage2::readOne(m_storage.get(), std::move(key));
    }

    auto readOne(const auto& key, storage2::DIRECT_TYPE direct)
        -> task::Task<task::AwaitableReturnType<
            std::invoke_result_t<storage2::ReadOne, Storage&, decltype(key)>>>
    {
        co_return co_await storage2::readOne(m_storage.get(), key, direct);
    }

    auto existsOne(auto key)
        -> task::Task<task::AwaitableReturnType<
            std::invoke_result_t<storage2::ExistsOne, Storage&, decltype(key)>>>
    {
        putSet(false, key);
        co_return co_await storage2::existsOne(m_storage.get(), std::move(key));
    }

    auto writeOne(auto key, auto value)
        -> task::Task<task::AwaitableReturnType<
            std::invoke_result_t<storage2::WriteOne, Storage&, decltype(key), decltype(value)>>>
    {
        putSet(true, key);
        co_await storage2::writeOne(m_storage.get(), std::move(key), std::move(value));
    }

    auto writeSome(::ranges::input_range auto keyValues)
        -> task::Task<task::AwaitableReturnType<
            std::invoke_result_t<storage2::WriteSome, Storage&, decltype(keyValues)>>>
    {
        for (auto&& [key, _] : keyValues)
        {
            putSet(true, key);
        }
        co_return co_await storage2::writeSome(m_storage.get(), std::move(keyValues));
    }

    task::Task<void> removeOne(auto key, auto&&... args)
    {
        putSet(true, key);
        co_await storage2::removeOne(
            m_storage.get(), std::move(key), std::forward<decltype(args)>(args)...);
    }

    auto removeSome(::ranges::input_range auto keys, auto&&... args)
        -> task::Task<task::AwaitableReturnType<std::invoke_result_t<storage2::RemoveSome, Storage&,
            decltype(keys), decltype(args)...>>>
    {
        for (auto&& key : keys)
        {
            putSet(true, key);
        }
        co_return co_await storage2::removeSome(
            m_storage.get(), std::move(keys), std::forward<decltype(args)>(args)...);
    }

    auto range(auto&&... args)
        -> task::Task<storage2::ReturnType<
            std::invoke_result_t<storage2::Range, Storage&, decltype(args)...>>>
    {
        co_return co_await storage2::range(
            m_storage.get(), std::forward<decltype(args)>(args)...);
    }

    friend auto& readWriteSet(ReadWriteSetStorage& storage) { return storage.m_readWriteSet; }
    friend auto const& readWriteSet(ReadWriteSetStorage const& storage)
    {
        return storage.m_readWriteSet;
    }

    friend void mergeWriteSet(ReadWriteSetStorage& storage, auto& inputWriteSet)
    {
        auto& writeMap = readWriteSet(inputWriteSet);
        for (auto& [key, flag] : writeMap)
        {
            if (flag.write)
            {
                storage.putSet(true, key);
            }
        }
    }

    // RAW: read after write
    friend bool hasRAWIntersection(ReadWriteSetStorage const& lhs, const auto& rhs)
    {
        auto const& lhsSet = readWriteSet(lhs);
        auto const& rhsSet = readWriteSet(rhs);

        if (::ranges::empty(lhsSet) || ::ranges::empty(rhsSet))
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
};

}  // namespace bcos::scheduler_v1
