#pragma once

#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-task/Trait.h"
#include <type_traits>

namespace bcos::transaction_executor
{

template <class Storage>
concept HasReadOneDirect = requires(Storage& storage) {
    requires !std::is_void_v<task::AwaitableReturnType<decltype(storage2::readOne(
        storage, std::declval<typename Storage::Key>(), storage2::DIRECT))>>;
};
template <class Storage>
concept HasReadSomeDirect = requires(Storage& storage) {
    requires RANGES::range<task::AwaitableReturnType<decltype(storage2::readSome(
        storage, std::declval<std::vector<typename Storage::Key>>(), storage2::DIRECT))>>;
};

template <class Storage>
class Rollbackable
{
public:
    constexpr static bool isRollbackable = true;
    using BackendStorage = Storage;

    struct Record
    {
        StateKey key;
        task::AwaitableReturnType<std::invoke_result_t<decltype(storage2::readOne), Storage&,
            typename Storage::Key, storage2::DIRECT_TYPE>>
            oldValue;
    };
    std::vector<Record> m_records;
    std::reference_wrapper<Storage> m_storage;

    using Savepoint = int64_t;
    using Key = typename Storage::Key;
    using Value = typename Storage::Value;

    Rollbackable(Storage& storage) : m_storage(storage) {}
};

template <class Storage>
concept IsRollbackable = std::remove_cvref_t<Storage>::isRollbackable;

template <IsRollbackable Rollbackable>
typename Rollbackable::Savepoint current(Rollbackable const& storage)
{
    return static_cast<int64_t>(storage.m_records.size());
}

template <IsRollbackable Rollbackable>
task::Task<void> rollback(Rollbackable& storage, typename Rollbackable::Savepoint savepoint)
{
    if (storage.m_records.empty())
    {
        co_return;
    }
    for (auto index = static_cast<int64_t>(storage.m_records.size()); index > savepoint; --index)
    {
        assert(index > 0);
        auto& record = storage.m_records[index - 1];
        if (record.oldValue)
        {
            co_await storage2::writeOne(
                storage.m_storage.get(), std::move(record.key), std::move(*record.oldValue));
        }
        else
        {
            co_await storage2::removeOne(storage.m_storage.get(), record.key, storage2::DIRECT);
        }
        storage.m_records.pop_back();
    }
    co_return;
}

template <IsRollbackable Rollbackable>
auto tag_invoke(storage2::tag_t<storage2::readSome> /*unused*/, Rollbackable& storage,
    RANGES::input_range auto&& keys)
    -> task::Task<task::AwaitableReturnType<std::invoke_result_t<storage2::ReadSome,
        typename Rollbackable::BackendStorage&, decltype(keys)>>>
{
    co_return co_await storage2::readSome(
        storage.m_storage.get(), std::forward<decltype(keys)>(keys));
}

template <IsRollbackable Rollbackable>
auto tag_invoke(storage2::tag_t<storage2::readOne> /*unused*/, Rollbackable& storage,
    auto&& key) -> task::Task<task::AwaitableReturnType<std::invoke_result_t<storage2::ReadOne,
                    typename Rollbackable::BackendStorage&, decltype(key)>>>
{
    co_return co_await storage2::readOne(storage.m_storage.get(), std::forward<decltype(key)>(key));
}

template <IsRollbackable Rollbackable>
auto tag_invoke(storage2::tag_t<storage2::writeSome> /*unused*/, Rollbackable& storage,
    RANGES::input_range auto&& keys, RANGES::input_range auto&& values)
    -> task::Task<task::AwaitableReturnType<std::invoke_result_t<storage2::WriteSome,
        typename Rollbackable::BackendStorage&, decltype(keys), decltype(values)>>>
    requires HasReadSomeDirect<typename Rollbackable::BackendStorage>
{
    auto oldValues = co_await storage2::readSome(storage.m_storage.get(), keys, storage2::DIRECT);
    for (auto&& [key, oldValue] : RANGES::views::zip(keys, oldValues))
    {
        storage.m_records.emplace_back(
            typename Rollbackable::Record{.key = typename Rollbackable::BackendStorage::Key{key},
                .oldValue = std::forward<decltype(oldValue)>(oldValue)});
    }

    co_return co_await storage2::writeSome(storage.m_storage.get(),
        std::forward<decltype(keys)>(keys), std::forward<decltype(values)>(values));
}

template <IsRollbackable Rollbackable>
auto tag_invoke(storage2::tag_t<storage2::writeOne> /*unused*/, Rollbackable& storage, auto&& key,
    auto&& value) -> task::Task<task::AwaitableReturnType<std::invoke_result_t<storage2::WriteOne,
                      typename Rollbackable::BackendStorage&, decltype(key), decltype(value)>>>
    requires HasReadOneDirect<typename Rollbackable::BackendStorage>
{
    auto& record = storage.m_records.emplace_back();
    record.key = key;
    record.oldValue = co_await storage2::readOne(storage.m_storage.get(), key, storage2::DIRECT);
    co_await storage2::writeOne(
        storage.m_storage.get(), record.key, std::forward<decltype(value)>(value));
}

template <IsRollbackable Rollbackable>
auto tag_invoke(storage2::tag_t<storage2::removeSome> /*unused*/, Rollbackable& storage,
    RANGES::input_range auto const& keys)
    -> task::Task<task::AwaitableReturnType<std::invoke_result_t<storage2::RemoveSome,
        std::add_lvalue_reference_t<typename Rollbackable::BackendStorage>, decltype(keys)>>>
{
    // Store values to history
    auto oldValues = co_await storage2::readSome(storage.m_storage.get(), keys, storage2::DIRECT);
    for (auto&& [key, value] : RANGES::views::zip(keys, oldValues))
    {
        if (value)
        {
            auto& record = storage.m_records.emplace_back(
                typename Rollbackable::Record{.key = StateKey{key}, .oldValue = std::move(*value)});
        }
    }

    co_return co_await storage2::removeSome(storage.m_storage.get(), keys);
}

template <IsRollbackable Rollbackable>
auto tag_invoke(
    bcos::storage2::tag_t<storage2::range> /*unused*/, Rollbackable& storage, auto&&... args)
    -> task::Task<storage2::ReturnType<std::invoke_result_t<storage2::Range,
        std::add_lvalue_reference_t<typename Rollbackable::BackendStorage>, decltype(args)...>>>
{
    co_return co_await storage2::range(
        storage.m_storage.get(), std::forward<decltype(args)>(args)...);
}

}  // namespace bcos::transaction_executor