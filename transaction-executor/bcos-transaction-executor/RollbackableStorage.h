#pragma once

#include "bcos-framework/storage2/Storage.h"
#include "bcos-task/Trait.h"
#include "bcos-utilities/Overloaded.h"
#include <range/v3/view/map.hpp>
#include <type_traits>

namespace bcos::executor_v1
{

template <class Storage>
concept HasReadOneRaw = requires(Storage& storage) {
    storage.readOneRaw(std::declval<typename Storage::Key>(), storage2::DIRECT);
};

template <class Storage>
concept HasReadSomeRaw = requires(Storage& storage) {
    storage.readSomeRaw(std::declval<std::vector<typename Storage::Key>>(), storage2::DIRECT);
};

template <class Storage>
class Rollbackable
{
public:
    using Key = typename Storage::Key;
    using Value = typename Storage::Value;
    using Savepoint = int64_t;

    Rollbackable(Storage& storage) : m_storage(storage) {}

    Savepoint current() { return static_cast<Savepoint>(m_records.size()); }

    task::Task<void> rollback(Savepoint savepoint)
    {
        if (m_records.empty())
        {
            co_return;
        }

        while (static_cast<int64_t>(m_records.size()) > savepoint)
        {
            auto& record = m_records.back();

            co_await std::visit(
                bcos::overloaded{[&](storage2::NOT_EXISTS_TYPE) -> task::Task<void> {
                                     co_await storage2::removeOne(
                                         m_storage.get(), std::move(record.key), storage2::DIRECT);
                                 },
                    [&](storage2::DELETED_TYPE) -> task::Task<void> {
                        co_await storage2::writeOne(
                            m_storage.get(), std::move(record.key), storage2::deleteItem);
                    },
                    [&](Value& value) -> task::Task<void> {
                        co_await storage2::writeOne(
                            m_storage.get(), std::move(record.key), std::move(value));
                    }},
                record.oldValue);

            m_records.pop_back();
        }
    }

private:
    struct Record
    {
        Key key;
        storage2::StorageValueType<Value> oldValue;
    };
    std::vector<Record> m_records;
    std::reference_wrapper<Storage> m_storage;

    template <class Keys>
        requires ::ranges::sized_range<Keys> && ::ranges::input_range<Keys>
    task::Task<void> storeOldValues(Keys keys, bool withEmpty)
    {
        auto& storage = m_storage.get();
        auto keyList = [&]() {
            if constexpr (std::is_same_v<::ranges::range_value_t<std::decay_t<Keys>>, Key>)
            {
                return ::ranges::views::all(keys);
            }
            else
            {
                return keys | ::ranges::views::transform([](auto&& key) { return Key(key); }) |
                       ::ranges::to<std::vector<Key>>;
            }
        }();
        auto oldValues = co_await storage.readSomeRaw(keyList, storage2::DIRECT);

        m_records.reserve(m_records.size() + keyList.size());
        for (auto&& [key, oldValue] : ::ranges::views::zip(keyList, oldValues))
        {
            if (!withEmpty && std::holds_alternative<storage2::NOT_EXISTS_TYPE>(oldValue))
            {
                continue;
            }
            m_records.emplace_back(
                typename Rollbackable::Record{.key = Key(key), .oldValue = std::move(oldValue)});
        }
    }

public:
    auto writeSome(::ranges::input_range auto keyValues)
        -> task::Task<task::AwaitableReturnType<std::invoke_result_t<storage2::WriteSome,
            std::add_lvalue_reference_t<Storage>, decltype(keyValues)>>>
        requires HasReadSomeRaw<Storage>
    {
        if constexpr (::ranges::borrowed_range<decltype(keyValues)>)
        {
            co_await storeOldValues(::ranges::views::keys(keyValues), true);
            co_return co_await storage2::writeSome(
                m_storage.get(), std::forward<decltype(keyValues)>(keyValues));
        }
        else
        {
            auto newKeyValues = ::ranges::to<std::vector>(keyValues);
            co_await storeOldValues(::ranges::views::keys(newKeyValues), true);
            co_return co_await storage2::writeSome(m_storage.get(), std::move(newKeyValues));
        }
    }

    auto readSome(::ranges::input_range auto keys) -> task::Task<task::AwaitableReturnType<
        std::invoke_result_t<storage2::ReadSome, Storage&, decltype(keys)>>>
    {
        co_return co_await storage2::readSome(m_storage.get(), std::move(keys));
    }

    auto readOne(auto key) -> task::Task<
        task::AwaitableReturnType<std::invoke_result_t<storage2::ReadOne, Storage&, decltype(key)>>>
    {
        co_return co_await storage2::readOne(m_storage.get(), std::move(key));
    }

    auto existsOne(auto key) -> task::Task<task::AwaitableReturnType<
        std::invoke_result_t<storage2::ExistsOne, Storage&, decltype(key)>>>
    {
        co_return co_await storage2::existsOne(m_storage.get(), std::move(key));
    }

    auto writeOne(auto key, auto value) -> task::Task<task::AwaitableReturnType<
        std::invoke_result_t<storage2::WriteOne, Storage&, decltype(key), decltype(value)>>>
        requires HasReadOneRaw<Storage>
    {
        auto& record = m_records.emplace_back();
        record.key = key;
        auto& storage = m_storage.get();
        record.oldValue = co_await storage.readOneRaw(key, storage2::DIRECT);
        co_await storage2::writeOne(m_storage.get(), std::move(key), std::move(value));
    }

    auto removeOne(auto key, auto&&... args)
        -> task::Task<task::AwaitableReturnType<std::invoke_result_t<storage2::RemoveOne,
            std::add_lvalue_reference_t<Storage>, decltype(key), decltype(args)...>>>
    {
        co_await storeOldValues(::ranges::views::single(key), false);
        co_return co_await storage2::removeOne(
            m_storage.get(), std::move(key), std::forward<decltype(args)>(args)...);
    }

    auto removeSome(::ranges::input_range auto keys)
        -> task::Task<task::AwaitableReturnType<std::invoke_result_t<storage2::RemoveSome,
            std::add_lvalue_reference_t<Storage>, decltype(keys)>>>
    {
        co_await storeOldValues(keys, false);
        co_return co_await storage2::removeSome(m_storage.get(), std::move(keys));
    }

    auto range(auto&&... args)
        -> task::Task<storage2::ReturnType<std::invoke_result_t<storage2::Range,
            std::add_lvalue_reference_t<Storage>, decltype(args)...>>>
    {
        co_return co_await storage2::range(m_storage.get(), std::forward<decltype(args)>(args)...);
    }
};


}  // namespace bcos::executor_v1
