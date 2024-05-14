#pragma once

#include "Storage.h"
#include "bcos-task/AwaitableValue.h"
#include "bcos-utilities/NullLock.h"
#include <oneapi/tbb/spin_rw_mutex.h>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/key.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/throw_exception.hpp>
#include <type_traits>
#include <utility>

namespace bcos::storage2::memory_storage
{

template <class Object>
concept HasMemberSize = requires(Object object) {
    { object.size() } -> std::integral;
};

using Empty = std::monostate;

enum Attribute : int
{
    NONE = 0,
    ORDERED = 1,
    CONCURRENT = 1 << 1,
    LRU = 1 << 2,
    LOGICAL_DELETION = 1 << 3
};

template <class KeyType, class ValueType = Empty, Attribute attribute = Attribute::NONE,
    class BucketHasher = void>
class MemoryStorage
{
public:
    constexpr static bool withOrdered = (attribute & Attribute::ORDERED) != 0;
    constexpr static bool withConcurrent = (attribute & Attribute::CONCURRENT) != 0;
    constexpr static bool withLRU = (attribute & Attribute::LRU) != 0;
    constexpr static bool withLogicalDeletion = (attribute & Attribute::LOGICAL_DELETION) != 0;

private:
    constexpr static unsigned BUCKETS_COUNT = 64;  // Magic number 64
    constexpr unsigned getBucketSize() { return withConcurrent ? BUCKETS_COUNT : 1; }
    static_assert(!withConcurrent || !std::is_void_v<BucketHasher>);

    constexpr static unsigned DEFAULT_CAPACITY = 32 * 1024 * 1024;  // For mru
    using Mutex = tbb::spin_rw_mutex;
    using Lock = std::conditional_t<withConcurrent, typename tbb::spin_rw_mutex::scoped_lock,
        utilities::NullLock>;
    using BucketMutex = std::conditional_t<withConcurrent, Mutex, Empty>;
    using DataValue = std::conditional_t<withLogicalDeletion, std::optional<ValueType>, ValueType>;

    struct Data
    {
        KeyType key;
        [[no_unique_address]] DataValue value;
    };

    using IndexType = std::conditional_t<withOrdered,
        boost::multi_index::ordered_unique<boost::multi_index::member<Data, KeyType, &Data::key>>,
        boost::multi_index::hashed_unique<boost::multi_index::member<Data, KeyType, &Data::key>>>;
    using Container = std::conditional_t<withLRU,
        boost::multi_index_container<Data,
            boost::multi_index::indexed_by<IndexType, boost::multi_index::sequenced<>>>,
        boost::multi_index_container<Data, boost::multi_index::indexed_by<IndexType>>>;
    struct Bucket
    {
        Container container;
        [[no_unique_address]] BucketMutex mutex;  // For concurrent
        [[no_unique_address]] std::conditional_t<withLRU, int64_t, Empty> capacity = {};  // LRU
    };
    using Buckets = std::conditional_t<withConcurrent, std::vector<Bucket>, std::array<Bucket, 1>>;

    Buckets m_buckets;
    [[no_unique_address]] std::conditional_t<withLRU, int64_t, Empty> m_maxCapacity;

    Bucket& getBucket(auto const& key) & noexcept
    {
        if constexpr (!withConcurrent)
        {
            return m_buckets[0];
        }
        auto index = getBucketIndex(key);

        auto& bucket = m_buckets[index];
        return bucket;
    }

    size_t getBucketIndex(auto const& key) const
    {
        if constexpr (!withConcurrent)
        {
            return 0;
        }
        else
        {
            auto hash = BucketHasher{}(key);
            return hash % m_buckets.size();
        }
    }

    void updateMRUAndCheck(
        Bucket& bucket, typename Container::template nth_index<0>::type::iterator entryIt)
        requires withLRU
    {
        auto& index = bucket.container.template get<1>();
        auto seqIt = index.iterator_to(*entryIt);
        index.relocate(index.end(), seqIt);

        while (bucket.capacity > m_maxCapacity && !bucket.container.empty())
        {
            auto const& item = index.front();
            bucket.capacity -= (getSize(item.key) + getSize(item.value));
            index.pop_front();
        }
    }

    static int64_t getSize(auto const& object)
    {
        using ObjectType = std::remove_cvref_t<decltype(object)>;
        if constexpr (HasMemberSize<ObjectType>)
        {
            return object.size();
        }
        // Treat any no-size() object as trivial, TODO: fix it
        return sizeof(ObjectType);
    }

public:
    using Key = KeyType;
    using Value = ValueType;

    explicit MemoryStorage(unsigned buckets = BUCKETS_COUNT)
    {
        if constexpr (withConcurrent)
        {
            m_buckets = decltype(m_buckets)(std::min(buckets, getBucketSize()));
        }
        if constexpr (withLRU)
        {
            m_maxCapacity = DEFAULT_CAPACITY;
        }
    }
    MemoryStorage(const MemoryStorage&) = delete;
    MemoryStorage(MemoryStorage&&) noexcept = default;
    MemoryStorage& operator=(const MemoryStorage&) = delete;
    MemoryStorage& operator=(MemoryStorage&&) noexcept = default;
    ~MemoryStorage() noexcept = default;

    void setMaxCapacity(int64_t capacity)
        requires withLRU
    {
        m_maxCapacity = capacity;
    }

    friend auto tag_invoke(bcos::storage2::tag_t<readSome> /*unused*/, MemoryStorage& storage,
        RANGES::input_range auto&& keys)
        -> task::AwaitableValue<std::vector<std::optional<ValueType>>>
    {
        task::AwaitableValue<std::vector<std::optional<ValueType>>> result;
        if constexpr (RANGES::sized_range<decltype(keys)>)
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
                result.value().emplace_back(it->value);

                if constexpr (std::decay_t<decltype(storage)>::withLRU)
                {
                    lock.upgrade_to_writer();
                    storage.updateMRUAndCheck(bucket, it);
                }
            }
            else
            {
                result.value().emplace_back(std::optional<ValueType>{});
            }
        }
        return result;
    }

    friend task::AwaitableValue<std::optional<ValueType>> tag_invoke(
        storage2::tag_t<storage2::readOne> /*unused*/, MemoryStorage& storage, auto&& key)
    {
        task::AwaitableValue<std::optional<ValueType>> result;
        auto& bucket = storage.getBucket(key);
        Lock lock(bucket.mutex, false);

        auto const& index = bucket.container.template get<0>();
        auto it = index.find(key);
        if (it != index.end())
        {
            if constexpr (std::decay_t<decltype(storage)>::withLRU)
            {
                lock.upgrade_to_writer();
                storage.updateMRUAndCheck(bucket, it);
            }

            result.value() = it->value;
        }
        return result;
    }

    friend task::AwaitableValue<void> tag_invoke(storage2::tag_t<storage2::writeSome> /*unused*/,
        MemoryStorage& storage, RANGES::input_range auto&& keys, RANGES::input_range auto&& values)
    {
        for (auto&& [key, value] : RANGES::views::zip(keys, values))
        {
            auto& bucket = storage.getBucket(key);
            Lock lock(bucket.mutex, true);
            auto const& index = bucket.container.template get<0>();

            std::conditional_t<std::decay_t<decltype(storage)>::withLRU, int64_t, Empty>
                updatedCapacity;
            if constexpr (std::decay_t<decltype(storage)>::withLRU)
            {
                updatedCapacity = getSize(key) + getSize(value);
            }

            typename Container::iterator it;
            if constexpr (std::decay_t<decltype(storage)>::withOrdered)
            {
                it = index.lower_bound(key);
            }
            else
            {
                it = index.find(key);
            }
            if (it != index.end() && std::equal_to<Key>{}(it->key, key))
            {
                if constexpr (std::decay_t<decltype(storage)>::withLRU)
                {
                    auto& existsValue = it->value;
                    updatedCapacity -= (getSize(key) + getSize(existsValue));
                }

                bucket.container.modify(
                    it, [newValue = std::forward<decltype(value)>(value)](Data& data) mutable {
                        data.value = std::forward<decltype(newValue)>(newValue);
                    });
            }
            else
            {
                it = bucket.container.emplace_hint(
                    it, Data{.key = KeyType(std::forward<decltype(key)>(key)),
                            .value = std::forward<decltype(value)>(value)});
            }

            if constexpr (std::decay_t<decltype(storage)>::withLRU)
            {
                bucket.capacity += updatedCapacity;
                storage.updateMRUAndCheck(bucket, it);
            }
        }

        return {};
    }

    friend task::AwaitableValue<void> tag_invoke(storage2::tag_t<storage2::removeSome> /*unused*/,
        MemoryStorage& storage, RANGES::input_range auto&& keys)
    {
        return removeSome(storage, std::forward<decltype(keys)>(keys), false);
    }

    friend task::AwaitableValue<void> tag_invoke(storage2::tag_t<storage2::removeSome> /*unused*/,
        MemoryStorage& storage, RANGES::input_range auto&& keys, DIRECT_TYPE /*unused*/)
    {
        return removeSome(storage, std::forward<decltype(keys)>(keys), true);
    }

    static task::AwaitableValue<void> removeSome(
        MemoryStorage& storage, RANGES::input_range auto&& keys, bool direct)
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
                if constexpr (std::decay_t<decltype(storage)>::withLogicalDeletion)
                {
                    if (!existsValue)
                    {
                        // Already deleted
                        return {};
                    }
                }

                if constexpr (withLRU)
                {
                    bucket.capacity -= (getSize(key) + getSize(existsValue));
                }

                if constexpr (withLogicalDeletion)
                {
                    if (!direct)
                    {
                        bucket.container.modify(it, [](Data& data) mutable { data.value.reset(); });
                    }
                    else
                    {
                        bucket.container.erase(it);
                    }
                }
                else
                {
                    bucket.container.erase(it);
                }
            }
            else
            {
                if constexpr (withLogicalDeletion)
                {
                    if constexpr (std::is_same_v<std::decay_t<decltype(key)>, KeyType>)
                    {
                        it = bucket.container.emplace_hint(it, Data{.key = key, .value = {}});
                    }
                    else
                    {
                        it = bucket.container.emplace_hint(it, Data{.key = Key{key}, .value = {}});
                    }
                }
            }
        }

        return {};
    }

    friend task::AwaitableValue<void> tag_invoke(
        storage2::tag_t<merge> /*unused*/, MemoryStorage& toStorage, MemoryStorage&& fromStorage)
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
        storage2::tag_t<merge> /*unused*/, MemoryStorage& toStorage, auto&& fromStorage)
    {
        auto iterator = co_await storage2::range(fromStorage);
        while (auto item = co_await iterator.next())
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
            using IteratorValue =
                std::conditional_t<withLogicalDeletion, const ValueType*, const ValueType&>;

            std::optional<std::tuple<const Key&, IteratorValue>> result;
            if (m_begin != m_end)
            {
                auto const& data = *m_begin;
                if constexpr (withLogicalDeletion)
                {
                    result.emplace(std::make_tuple(
                        std::cref(data.key), data.value ? std::addressof(*(data.value)) : nullptr));
                }
                else
                {
                    result.emplace(std::make_tuple(std::cref(data.key), std::cref(data.value)));
                }
                ++m_begin;
            }
            return task::AwaitableValue(std::move(result));
        }
    };

    friend auto tag_invoke(
        bcos::storage2::tag_t<storage2::range> /*unused*/, MemoryStorage const& storage)
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
