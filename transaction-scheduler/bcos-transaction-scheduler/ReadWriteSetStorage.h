#pragma once
#include "bcos-framework/storage2/Storage.h"
#include <bcos-task/Trait.h>
#include <type_traits>

namespace bcos::transaction_scheduler
{

template <class Storage, class KeyType>
class ReadWriteSetStorage
{
public:
    constexpr static bool isReadWriteSetStorage = true;
    std::reference_wrapper<std::remove_reference_t<Storage>> m_storage;
    struct ReadWriteFlag
    {
        bool read = false;
        bool write = false;
    };
    std::unordered_map<size_t, ReadWriteFlag> m_readWriteSet;

    using Key = KeyType;
    using Value = typename task::AwaitableReturnType<decltype(storage2::readOne(
        m_storage.get(), std::declval<KeyType>()))>::value_type;
    using BackendStorage = Storage;

    ReadWriteSetStorage(Storage& storage) : m_storage(std::ref(storage)) {}
};

template <class Storage>
concept IsReadWriteSetStorage = std::remove_cvref_t<Storage>::isReadWriteSetStorage;

template <IsReadWriteSetStorage ReadWriteSetStorage>
void putSet(ReadWriteSetStorage& storage, bool write, size_t hash)
{
    auto [it, inserted] = storage.m_readWriteSet.try_emplace(
        hash, typename ReadWriteSetStorage::ReadWriteFlag{.read = !write, .write = write});
    if (!inserted)
    {
        it->second.write |= write;
        it->second.read |= (!write);
    }
}

template <IsReadWriteSetStorage ReadWriteSetStorage>
void putSet(ReadWriteSetStorage& storage, bool write, auto const& key)
{
    auto hash = std::hash<typename ReadWriteSetStorage::Key>{}(key);
    putSet(storage, write, hash);
}

template <IsReadWriteSetStorage ReadWriteSetStorage>
auto tag_invoke(storage2::tag_t<storage2::readSome> /*unused*/, ReadWriteSetStorage& storage,
    RANGES::input_range auto&& keys)
    -> task::Task<task::AwaitableReturnType<std::invoke_result_t<storage2::ReadSome,
        typename ReadWriteSetStorage::BackendStorage&, decltype(keys)>>>
{
    for (auto&& key : keys)
    {
        putSet(storage, false, std::forward<decltype(key)>(key));
    }
    co_return co_await storage2::readSome(
        storage.m_storage.get(), std::forward<decltype(keys)>(keys));
}

template <IsReadWriteSetStorage ReadWriteSetStorage>
auto tag_invoke(storage2::tag_t<storage2::readSome> /*unused*/, ReadWriteSetStorage& storage,
    RANGES::input_range auto&& keys, storage2::DIRECT_TYPE direct)
    -> task::Task<task::AwaitableReturnType<std::invoke_result_t<storage2::ReadSome,
        typename ReadWriteSetStorage::BackendStorage&, decltype(keys)>>>
{
    co_return co_await storage2::readSome(
        storage.m_storage.get(), std::forward<decltype(keys)>(keys), direct);
}

template <IsReadWriteSetStorage ReadWriteSetStorage>
auto tag_invoke(storage2::tag_t<storage2::readOne> /*unused*/, ReadWriteSetStorage& storage,
    auto&& key) -> task::Task<task::AwaitableReturnType<std::invoke_result_t<storage2::ReadOne,
                    typename ReadWriteSetStorage::BackendStorage&, decltype(key)>>>
{
    putSet(storage, false, key);
    co_return co_await storage2::readOne(storage.m_storage.get(), std::forward<decltype(key)>(key));
}

template <IsReadWriteSetStorage ReadWriteSetStorage>
auto tag_invoke(storage2::tag_t<storage2::readOne> /*unused*/, ReadWriteSetStorage& storage,
    auto&& key, storage2::DIRECT_TYPE direct)
    -> task::Task<task::AwaitableReturnType<std::invoke_result_t<storage2::ReadOne,
        typename ReadWriteSetStorage::BackendStorage&, decltype(key)>>>
{
    co_return co_await storage2::readOne(
        storage.m_storage.get(), std::forward<decltype(key)>(key), direct);
}

template <IsReadWriteSetStorage ReadWriteSetStorage>
auto tag_invoke(storage2::tag_t<storage2::writeSome> /*unused*/, ReadWriteSetStorage& storage,
    RANGES::input_range auto&& keys, RANGES::input_range auto&& values)
    -> task::Task<task::AwaitableReturnType<std::invoke_result_t<storage2::WriteSome,
        typename ReadWriteSetStorage::BackendStorage&, decltype(keys), decltype(values)>>>
{
    for (auto&& key : keys)
    {
        putSet(storage, true, key);
    }
    co_return co_await storage2::writeSome(
        storage.m_storage.get(), keys, std::forward<decltype(values)>(values));
}

template <IsReadWriteSetStorage ReadWriteSetStorage>
auto tag_invoke(storage2::tag_t<storage2::removeSome> /*unused*/, ReadWriteSetStorage& storage,
    RANGES::input_range auto const& keys, auto&&... args)
    -> task::Task<task::AwaitableReturnType<std::invoke_result_t<storage2::RemoveSome,
        std::add_lvalue_reference_t<typename ReadWriteSetStorage::BackendStorage>, decltype(keys),
        decltype(args)...>>>
{
    for (auto&& key : keys)
    {
        putSet(storage, true, key);
    }
    co_return co_await storage2::removeSome(
        storage.m_storage.get(), keys, std::forward<decltype(args)>(args)...);
}

template <IsReadWriteSetStorage ReadWriteSetStorage>
auto tag_invoke(bcos::storage2::tag_t<storage2::range> /*unused*/, ReadWriteSetStorage& storage,
    auto&&... args) -> task::Task<storage2::ReturnType<std::invoke_result_t<storage2::Range,
                        std::add_lvalue_reference_t<typename ReadWriteSetStorage::BackendStorage>,
                        decltype(args)...>>>
{
    co_return co_await storage2::range(
        storage.m_storage.get(), std::forward<decltype(args)>(args)...);
}

template <IsReadWriteSetStorage ReadWriteSetStorage>
auto& readWriteSet(ReadWriteSetStorage& storage)
{
    return storage.m_readWriteSet;
}

template <IsReadWriteSetStorage ReadWriteSetStorage>
auto const& readWriteSet(ReadWriteSetStorage const& storage)
{
    return storage.m_readWriteSet;
}

template <IsReadWriteSetStorage ReadWriteSetStorage>
void mergeWriteSet(ReadWriteSetStorage& storage, auto& inputWriteSet)
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
template <IsReadWriteSetStorage ReadWriteSetStorage>
bool hasRAWIntersection(ReadWriteSetStorage const& lhs, const auto& rhs)
{
    auto const& lhsSet = readWriteSet(lhs);
    auto const& rhsSet = readWriteSet(rhs);

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

}  // namespace bcos::transaction_scheduler