#pragma once

#include "bcos-framework/storage2/Storage.h"
#include "bcos-task/AwaitableValue.h"
#include "bcos-utilities/Overloaded.h"
#include <oneapi/tbb/spin_rw_mutex.h>
#include <boost/throw_exception.hpp>
#include <type_traits>
#include <utility>
#include <variant>

namespace bcos::storage2::memory_storage
{

template <class KeyType, class ValueType, class BucketHasher>
class ParallelVersionStorage
{
public:
    using Key = KeyType;
    using Value = ValueType;

    explicit ParallelVersionStorage(unsigned buckets = BUCKETS_COUNT) : m_buckets() {}
    ParallelVersionStorage(const ParallelVersionStorage&) = delete;
    ParallelVersionStorage(ParallelVersionStorage&&) noexcept = default;
    ParallelVersionStorage& operator=(const ParallelVersionStorage&) = delete;
    ParallelVersionStorage& operator=(ParallelVersionStorage&&) noexcept = default;
    ~ParallelVersionStorage() noexcept = default;

private:
    constexpr static unsigned BUCKETS_COUNT = 64;  // Magic number 64
    using Mutex = tbb::spin_rw_mutex;
    using Lock = typename tbb::spin_rw_mutex::scoped_lock;
    using DataValueType = std::optional<Value>;

    struct Data
    {
        Key key;
        [[no_unique_address]] DataValueType value;
    };

    using Container = std::map<Key, Value>;
    struct Bucket
    {
        Container container;
        int index{};
        Mutex mutex;
    };
    using Buckets = std::vector<Bucket>;
    Buckets m_buckets;

    Bucket& getBucket(auto const& key) & noexcept
    {
        auto index = getBucketIndex(key);
        auto& bucket = m_buckets[index];
        return bucket;
    }

    size_t getBucketIndex(auto const& key) const
    {
        auto hash = BucketHasher{}(key);
        return hash % m_buckets.size();
    }

    friend auto tag_invoke(bcos::storage2::tag_t<readSome> /*unused*/,
        ParallelVersionStorage& storage, RANGES::input_range auto&& keys)
        -> task::AwaitableValue<std::vector<std::optional<ValueType>>>
    {
        task::AwaitableValue<std::vector<std::optional<ValueType>>> result;
        if (RANGES::sized_range<decltype(keys)>)
        {
            result.value().reserve(RANGES::size(keys));
        }

        for (auto&& key : keys)
        {
            auto& bucket = storage.getBucket(key);
            Lock lock(bucket.mutex, false);

            auto const& index = bucket.container.template get<0>();
            auto it = index.find(key);
            if (it != index.end())
            {
                result.value().emplace_back(it->second);
            }
            else
            {
                result.value().emplace_back(std::optional<ValueType>{});
            }
        }
        return result;
    }

    friend task::AwaitableValue<std::optional<ValueType>> tag_invoke(
        storage2::tag_t<storage2::readOne> /*unused*/, ParallelVersionStorage& storage, auto&& key)
    {
        task::AwaitableValue<std::optional<ValueType>> result;
        auto& bucket = storage.getBucket(key);
        Lock lock(bucket.mutex, false);

        auto const& index = bucket.container.template get<0>();
        auto it = index.find(key);
        if (it != index.end())
        {
            std::visit(
                bcos::overloaded{[&](ValueType const& value) { result.value().emplace(value); },
                    [&](Deleted const&) {}},
                it->value);
        }
        return result;
    }

    friend task::AwaitableValue<void> tag_invoke(storage2::tag_t<storage2::writeSome> /*unused*/,
        ParallelVersionStorage& storage, RANGES::input_range auto&& keys,
        RANGES::input_range auto&& values)
    {
        for (auto&& [key, value] : RANGES::views::zip(keys, values))
        {
            auto& bucket = storage.getBucket(key);
            Lock lock(bucket.mutex, true);
            auto const& index = bucket.container.template get<0>();

            auto it = index.lower_bound(key);
            if (it != index.end() && std::equal_to<Key>{}(it->key, key))
            {
                it->value = std::forward<decltype(value)>(value);
            }
            else
            {
                it = bucket.container.emplace_hint(
                    it, std::forward<decltype(key)>(key), std::forward<decltype(value)>(value));
            }
        }

        return {};
    }

    friend task::AwaitableValue<void> tag_invoke(storage2::tag_t<storage2::removeSome> /*unused*/,
        ParallelVersionStorage& storage, RANGES::input_range auto&& keys)
    {
        for (auto&& key : keys)
        {
            auto& bucket = storage.getBucket(key);
            Lock lock(bucket.mutex, true);
            auto const& index = bucket.container.template get<0>();

            auto it = index.find(key);
            if (it != index.end())
            {
                auto& existsValue = it->value;
                if (std::holds_alternative<Deleted>(existsValue))
                {
                    // Already deleted
                    return {};
                }

                bucket.container.modify(it,
                    [](Data& data) mutable { data.value.template emplace<Deleted>(Deleted{}); });
            }
            else
            {
                if constexpr (std::decay_t<decltype(storage)>::withLogicalDeletion)
                {
                    it = bucket.container.emplace_hint(it, Data{.key = key, .value = Deleted{}});
                }
            }
        }

        return {};
    }


    friend task::AwaitableValue<void> tag_invoke(storage2::tag_t<merge> /*unused*/,
        ParallelVersionStorage& toStorage, ParallelVersionStorage&& fromStorage)
        requires(!std::is_const_v<decltype(fromStorage)>)
    {
        for (auto&& [bucket, fromBucket] :
            RANGES::views::zip(toStorage.m_buckets, fromStorage.m_buckets))
        {
            Lock toLock(bucket.mutex, true);
            Lock fromLock(fromBucket.mutex, true);

            auto& toIndex = bucket.container.template get<0>();
            auto& fromIndex = fromBucket.container.template get<0>();

            if (toIndex.empty())
            {
                toIndex.swap(fromIndex);
                continue;
            }
            auto hintIt = toIndex.end();
            while (!fromIndex.empty())
            {
                auto node = fromIndex.extract(fromIndex.begin());
                hintIt = toIndex.insert(hintIt, std::move(node));
                if (!node.empty())
                {
                    hintIt = toIndex.insert(toIndex.erase(hintIt), std::move(node));
                }
            }
        }
        return {};
    }

    friend task::Task<void> tag_invoke(
        storage2::tag_t<merge> /*unused*/, ParallelVersionStorage& toStorage, auto&& fromStorage)
    {
        auto range = co_await storage2::range(fromStorage);
        while (auto item = co_await range.next())
        {
            auto&& [key, value] = *item;
            if constexpr (std::is_pointer_v<decltype(value)>)
            {
                if (value)
                {
                    co_await storage2::writeOne(toStorage, key, *value);
                }
                else
                {
                    co_await storage2::removeOne(toStorage, key);
                }
            }
            else
            {
                co_await storage2::writeOne(toStorage, key, value);
            }
        }
    }

    template <class Begin, class End>
    class Iterator
    {
    private:
        Begin m_begin;
        End m_end;

    public:
        Iterator(Begin begin, End end) : m_begin(begin), m_end(end) {}

        auto next()
        {
            if constexpr (withLogicalDeletion)
            {
                std::optional<std::tuple<const Key&, const ValueType*>> result;
                if (m_begin != m_end)
                {
                    auto const& data = *m_begin;
                    result.emplace(std::make_tuple(
                        std::cref(data.key), std::holds_alternative<Deleted>(data.value) ?
                                                 nullptr :
                                                 std::addressof(std::get<Value>(data.value))));
                    ++m_begin;
                }
                return task::AwaitableValue(std::move(result));
            }
            else
            {
                std::optional<std::tuple<const Key&, const ValueType&>> result;
                if (m_begin != m_end)
                {
                    auto const& data = *m_begin;
                    result.emplace(std::make_tuple(std::cref(data.key), std::cref(data.value)));
                    ++m_begin;
                }
                return task::AwaitableValue(std::move(result));
            }
        }
    };

    friend auto tag_invoke(
        bcos::storage2::tag_t<storage2::range> /*unused*/, ParallelVersionStorage const& storage)
        requires(!withConcurrent)
    {
        return task::AwaitableValue(Iterator<decltype(storage.m_buckets[0].container.begin()),
            decltype(storage.m_buckets[0].container.end())>(
            storage.m_buckets[0].container.begin(), storage.m_buckets[0].container.end()));
    }

    bool empty() const
    {
        bool allEmpty = true;
        for (auto& bucket : m_buckets)
        {
            Lock lock(bucket.mutex, false);
            if (!bucket.container.empty())
            {
                allEmpty = false;
                break;
            }
        }

        return allEmpty;
    }
};

}  // namespace bcos::storage2::memory_storage
