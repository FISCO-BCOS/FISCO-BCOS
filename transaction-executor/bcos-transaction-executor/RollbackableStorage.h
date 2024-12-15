#pragma once

#include "bcos-framework/storage2/Storage.h"
#include "bcos-task/Trait.h"
#include <range/v3/view/map.hpp>
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
    requires ::ranges::range<task::AwaitableReturnType<decltype(storage2::readSome(
        storage, std::declval<std::vector<typename Storage::Key>>(), storage2::DIRECT))>>;
};

template <class Storage>
class Rollbackable
{
public:
    using Key = typename Storage::Key;
    using Value = typename Storage::Value;
    using Savepoint = int64_t;

    Rollbackable(Storage& storage) : m_storage(storage) {}

private:
    struct Record
    {
        Key key;
        task::AwaitableReturnType<
            std::invoke_result_t<storage2::ReadOne, Storage&, Key, storage2::DIRECT_TYPE>>
            oldValue;
    };
    std::vector<Record> m_records;
    std::reference_wrapper<Storage> m_storage;

    task::Task<void> storeOldValues(auto&& keys, bool withEmpty)
    {
        auto oldValues = co_await storage2::readSome(m_storage.get(), keys, storage2::DIRECT);
        m_records.reserve(m_records.size() + ::ranges::size(keys));
        for (auto&& [key, oldValue] : ranges::views::zip(keys, oldValues))
        {
            if (oldValue || withEmpty)
            {
                m_records.emplace_back(typename Rollbackable::Record{
                    .key = Key(key), .oldValue = std::forward<decltype(oldValue)>(oldValue)});
            }
        }
    }

    friend Savepoint current(Rollbackable const& storage)
    {
        return static_cast<int64_t>(storage.m_records.size());
    }

    friend task::Task<void> rollback(Rollbackable& storage, Savepoint savepoint)
    {
        if (storage.m_records.empty())
        {
            co_return;
        }
        for (auto index = static_cast<int64_t>(storage.m_records.size()); index > savepoint;
             --index)
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

    friend auto tag_invoke(storage2::tag_t<storage2::writeSome> /*unused*/, Rollbackable& storage,
        ::ranges::input_range auto&& keyValues)
        -> task::Task<task::AwaitableReturnType<std::invoke_result_t<storage2::WriteSome,
            std::add_lvalue_reference_t<Storage>, decltype(keyValues)>>>
        requires HasReadSomeDirect<Storage>
    {
        if constexpr (::ranges::borrowed_range<decltype(keyValues)>)
        {
            co_await storage.storeOldValues(::ranges::views::keys(keyValues), true);
            co_return co_await storage2::writeSome(
                storage.m_storage.get(), std::forward<decltype(keyValues)>(keyValues));
        }
        else
        {
            auto newKeyValues = ::ranges::to<std::vector>(keyValues);
            co_await storage.storeOldValues(::ranges::views::keys(newKeyValues), true);
            co_return co_await storage2::writeSome(
                storage.m_storage.get(), std::move(newKeyValues));
        }
    }

    friend auto tag_invoke(storage2::tag_t<storage2::readSome> /*unused*/, Rollbackable& storage,
        ::ranges::input_range auto&& keys)
        -> task::Task<task::AwaitableReturnType<
            std::invoke_result_t<storage2::ReadSome, Storage&, decltype(keys)>>>
    {
        co_return co_await storage2::readSome(
            storage.m_storage.get(), std::forward<decltype(keys)>(keys));
    }

    friend auto tag_invoke(storage2::tag_t<storage2::readOne> /*unused*/, Rollbackable& storage,
        auto&& key) -> task::Task<task::AwaitableReturnType<std::invoke_result_t<storage2::ReadOne,
                        Storage&, decltype(key)>>>
    {
        co_return co_await storage2::readOne(
            storage.m_storage.get(), std::forward<decltype(key)>(key));
    }

    friend auto tag_invoke(storage2::tag_t<storage2::writeOne> /*unused*/, Rollbackable& storage,
        auto&& key, auto&& value)
        -> task::Task<task::AwaitableReturnType<
            std::invoke_result_t<storage2::WriteOne, Storage&, decltype(key), decltype(value)>>>
        requires HasReadOneDirect<Storage>
    {
        auto& record = storage.m_records.emplace_back();
        record.key = key;
        record.oldValue =
            co_await storage2::readOne(storage.m_storage.get(), key, storage2::DIRECT);
        co_await storage2::writeOne(
            storage.m_storage.get(), record.key, std::forward<decltype(value)>(value));
    }

    friend auto tag_invoke(storage2::tag_t<storage2::removeSome> /*unused*/, Rollbackable& storage,
        ::ranges::input_range auto const& keys)
        -> task::Task<task::AwaitableReturnType<std::invoke_result_t<storage2::RemoveSome,
            std::add_lvalue_reference_t<Storage>, decltype(keys)>>>
    {
        co_await storage.storeOldValues(keys, false);
        co_return co_await storage2::removeSome(storage.m_storage.get(), keys);
    }

    friend auto tag_invoke(
        bcos::storage2::tag_t<storage2::range> /*unused*/, Rollbackable& storage, auto&&... args)
        -> task::Task<task::AwaitableReturnType<std::invoke_result_t<storage2::Range,
            std::add_lvalue_reference_t<Storage>, decltype(args)...>>>
    {
        co_return co_await storage2::range(
            storage.m_storage.get(), std::forward<decltype(args)>(args)...);
    }
};

}  // namespace bcos::transaction_executor
