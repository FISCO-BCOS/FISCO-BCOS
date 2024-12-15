#pragma once
#include "bcos-framework/storage2/Storage.h"
#include <bcos-task/Trait.h>
#include <type_traits>
#include <utility>

namespace bcos::transaction_scheduler
{

template <class StorageType, class KeyType>
class ReadWriteSetStorage
{
private:
    std::reference_wrapper<std::remove_reference_t<StorageType>> m_storage;

public:
    using Key = KeyType;
    using Value = typename task::AwaitableReturnType<std::invoke_result_t<storage2::ReadOne,
        std::add_lvalue_reference_t<StorageType>, KeyType>>::value_type;
    ReadWriteSetStorage(StorageType& storage) : m_storage(std::ref(storage)) {}

private:
    struct ReadWriteFlag
    {
        bool read = false;
        bool write = false;
    };
    std::unordered_map<size_t, ReadWriteFlag> m_readWriteSet;
    using Storage = StorageType;

    friend void putSet(ReadWriteSetStorage& storage, bool write, size_t hash)
    {
        auto [it, inserted] = storage.m_readWriteSet.try_emplace(
            hash, typename ReadWriteSetStorage::ReadWriteFlag{.read = !write, .write = write});
        if (!inserted)
        {
            it->second.write |= write;
            it->second.read |= (!write);
        }
    }
    friend void putSet(ReadWriteSetStorage& storage, bool write, auto const& key)
    {
        auto hash = std::hash<Key>{}(key);
        putSet(storage, write, hash);
    }

    friend auto tag_invoke(storage2::tag_t<storage2::readSome> /*unused*/,
        ReadWriteSetStorage& storage, ::ranges::input_range auto&& keys)
        -> task::Task<task::AwaitableReturnType<
            std::invoke_result_t<storage2::ReadSome, Storage&, decltype(keys)>>>
    {
        for (auto&& key : keys)
        {
            putSet(storage, false, std::forward<decltype(key)>(key));
        }
        co_return co_await storage2::readSome(
            storage.m_storage.get(), std::forward<decltype(keys)>(keys));
    }

    friend auto tag_invoke(storage2::tag_t<storage2::readSome> /*unused*/,
        ReadWriteSetStorage& storage, ::ranges::input_range auto&& keys,
        storage2::DIRECT_TYPE direct)
        -> task::Task<task::AwaitableReturnType<std::invoke_result_t<storage2::ReadSome,
            std::add_lvalue_reference_t<Storage>, decltype(keys)>>>
    {
        co_return co_await storage2::readSome(
            storage.m_storage.get(), std::forward<decltype(keys)>(keys), direct);
    }

    friend auto tag_invoke(storage2::tag_t<storage2::readOne> /*unused*/,
        ReadWriteSetStorage& storage,
        auto&& key) -> task::Task<task::AwaitableReturnType<std::invoke_result_t<storage2::ReadOne,
                        std::add_lvalue_reference_t<Storage>, decltype(key)>>>
    {
        putSet(storage, false, key);
        co_return co_await storage2::readOne(
            storage.m_storage.get(), std::forward<decltype(key)>(key));
    }

    friend auto tag_invoke(storage2::tag_t<storage2::readOne> /*unused*/,
        ReadWriteSetStorage& storage, auto&& key, storage2::DIRECT_TYPE direct)
        -> task::Task<task::AwaitableReturnType<
            std::invoke_result_t<storage2::ReadOne, Storage&, decltype(key)>>>
    {
        co_return co_await storage2::readOne(
            storage.m_storage.get(), std::forward<decltype(key)>(key), direct);
    }

    friend auto tag_invoke(storage2::tag_t<storage2::writeOne> /*unused*/,
        ReadWriteSetStorage& storage, auto&& key, auto&& value)
        -> task::Task<task::AwaitableReturnType<
            std::invoke_result_t<storage2::WriteOne, Storage&, decltype(key), decltype(value)>>>
    {
        putSet(storage, true, key);
        co_await storage2::writeOne(storage.m_storage.get(), std::forward<decltype(key)>(key),
            std::forward<decltype(value)>(value));
    }

    friend auto tag_invoke(storage2::tag_t<storage2::writeSome> /*unused*/,
        ReadWriteSetStorage& storage, ::ranges::input_range auto&& keyValues)
        -> task::Task<task::AwaitableReturnType<
            std::invoke_result_t<storage2::WriteSome, Storage&, decltype(keyValues)>>>
    {
        for (auto&& [key, _] : keyValues)
        {
            putSet(storage, true, key);
        }
        co_return co_await storage2::writeSome(
            storage.m_storage.get(), std::forward<decltype(keyValues)>(keyValues));
    }

    friend auto tag_invoke(storage2::tag_t<storage2::removeSome> /*unused*/,
        ReadWriteSetStorage& storage, ::ranges::input_range auto const& keys, auto&&... args)
        -> task::Task<task::AwaitableReturnType<std::invoke_result_t<storage2::RemoveSome, Storage&,
            decltype(keys), decltype(args)...>>>
    {
        for (auto&& key : keys)
        {
            putSet(storage, true, key);
        }
        co_return co_await storage2::removeSome(
            storage.m_storage.get(), keys, std::forward<decltype(args)>(args)...);
    }

    friend auto tag_invoke(bcos::storage2::tag_t<storage2::range> /*unused*/,
        ReadWriteSetStorage& storage, auto&&... args)
        -> task::Task<task::AwaitableReturnType<
            std::invoke_result_t<storage2::Range, Storage&, decltype(args)...>>>
    {
        co_return co_await storage2::range(
            storage.m_storage.get(), std::forward<decltype(args)>(args)...);
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
                putSet(storage, true, key);
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

}  // namespace bcos::transaction_scheduler
