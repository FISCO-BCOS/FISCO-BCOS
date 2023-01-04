#pragma once

#include "Storage.h"
#include "bcos-task/Task.h"
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/key.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/throw_exception.hpp>
#include <forward_list>
#include <functional>
#include <mutex>
#include <range/v3/view/transform.hpp>
#include <set>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>

namespace bcos::storage2::concurrent_ordered_storage
{

template <class Object>
concept HasMemberSize = requires(Object object)
{
    // clang-format off
    { object.size() } -> std::integral;
    // clang-format on
};
template <bool withMRU, class KeyType, class ValueType>
class ConcurrentOrderedStorage
{
private:
    constexpr static unsigned MAX_BUCKETS = 64;  // Support up to 64 buckets, enough?
    constexpr static unsigned DEFAULT_CAPACITY = 32L * 1024 * 1024;

    struct Empty
    {
    };

    struct Data
    {
        KeyType key;
        [[no_unique_address]] ValueType value;
    };

    using OrderedContainer = boost::multi_index_container<Data,
        boost::multi_index::indexed_by<boost::multi_index::ordered_unique<
            boost::multi_index::member<Data, KeyType, &Data::key>>>>;
    using MRUOrderedContainer = boost::multi_index_container<Data,
        boost::multi_index::indexed_by<boost::multi_index::ordered_unique<
                                           boost::multi_index::member<Data, KeyType, &Data::key>>,
            boost::multi_index::sequenced<>>>;
    using Container = std::conditional_t<withMRU, MRUOrderedContainer, OrderedContainer>;

    struct Bucket
    {
        Container container;
        std::mutex mutex;
        [[no_unique_address]] std::conditional_t<withMRU, int64_t, Empty> capacity = {};
    };

    std::vector<Bucket> m_buckets;
    [[no_unique_address]] std::conditional_t<withMRU, int64_t, Empty> m_maxCapacity = {};

    std::tuple<std::reference_wrapper<Bucket>, std::unique_lock<std::mutex>> getBucket(
        auto const& key)
    {
        auto index = getBucketIndex(key);

        auto& bucket = m_buckets[index];
        return std::make_tuple(std::ref(bucket), std::unique_lock<std::mutex>(bucket.mutex));
    }

    std::reference_wrapper<Bucket> getBucketByIndex(size_t index)
    {
        return std::ref(m_buckets[index]);
    }

    size_t getBucketIndex(auto const& key) const
    {
        auto hash = std::hash<std::remove_cvref_t<decltype(key)>>{}(key);
        return hash % m_buckets.size();
    }

    void updateMRUAndCheck(Bucket& bucket,
        typename Container::template nth_index<0>::type::iterator entryIt) requires withMRU
    {
        auto& index = bucket.container.template get<1>();
        auto seqIt = index.iterator_to(*entryIt);
        index.relocate(index.end(), seqIt);

        size_t clearCount = 0;
        while (bucket.capacity > m_maxCapacity && !bucket.container.empty())
        {
            auto const& item = index.front();
            bucket.capacity -= getSize(item.value);

            index.pop_front();
            ++clearCount;
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
    ConcurrentOrderedStorage(unsigned buckets = std::thread::hardware_concurrency()) requires(
        !withMRU)
      : m_buckets(std::min(buckets, MAX_BUCKETS))
    {}

    ConcurrentOrderedStorage(unsigned buckets = std::thread::hardware_concurrency(),
        int64_t capacityPerBucket = DEFAULT_CAPACITY) requires withMRU
      : m_buckets(std::min(buckets, MAX_BUCKETS)),
        m_maxCapacity(capacityPerBucket)
    {}

    class ReadIterator
    {
    public:
        friend class ConcurrentOrderedStorage;
        using Key = const KeyType&;
        using Value = const ValueType&;

        task::ValueAwaitable<bool> hasValue() { return {*m_it != nullptr}; }
        task::ValueAwaitable<bool> next()
        {
            if (!m_started)
            {
                m_started = true;
                return {m_it != m_iterators.end()};
            }
            return {(++m_it) != m_iterators.end()};
        }
        task::ValueAwaitable<Key> key() const { return {(*m_it)->key}; }
        task::ValueAwaitable<Value> value() const { return {(*m_it)->value}; }

        void release() { m_bucketLocks.clear(); }

    private:
        typename std::vector<const Data*>::iterator m_it;
        std::vector<const Data*> m_iterators;
        std::forward_list<std::unique_lock<std::mutex>> m_bucketLocks;
        bool m_started = false;
    };

    class SeekIterator
    {
    public:
        friend class ConcurrentOrderedStorage;
        using Key = const KeyType&;
        using Value = const ValueType&;

        task::ValueAwaitable<bool> next()
        {
            if (!m_started)
            {
                m_started = true;
                return {m_it != m_end};
            }
            return {(++m_it) != m_end};
        }
        task::ValueAwaitable<Key> key() const { return {m_it->key}; }
        task::ValueAwaitable<Value> value() const { return {m_it->value}; }

        void release() { m_bucketLock.unlock(); }

    private:
        typename Container::iterator m_it;
        typename Container::iterator m_end;
        std::unique_lock<std::mutex> m_bucketLock;
        bool m_started = false;
    };

    task::ValueAwaitable<ReadIterator> read(RANGES::input_range auto const& keys)
    {
        task::ValueAwaitable<ReadIterator> outputAwaitable(ReadIterator{});
        ReadIterator& output = outputAwaitable.value();
        if constexpr (RANGES::sized_range<std::remove_cvref_t<decltype(keys)>>)
        {
            output.m_iterators.reserve(RANGES::size(keys));
        }

        std::bitset<MAX_BUCKETS> locks;
        for (auto&& key : keys)
        {
            auto bucketIndex = getBucketIndex(key);
            auto bucket = getBucketByIndex(bucketIndex);
            if (!locks[bucketIndex])
            {
                locks[bucketIndex] = true;
                output.m_bucketLocks.emplace_front(std::unique_lock(bucket.get().mutex));
            }

            auto const& index = bucket.get().container.template get<0>();

            auto it = index.find(key);
            if (it != index.end())
            {
                if constexpr (withMRU)
                {
                    updateMRUAndCheck(bucket, it);
                }
                output.m_iterators.emplace_back(std::addressof(*it));
            }
            else
            {
                output.m_iterators.emplace_back(nullptr);
            }
        }
        output.m_it = output.m_iterators.begin();

        return outputAwaitable;
    }

    task::ValueAwaitable<SeekIterator> seek(auto const& key)
    {
        auto [bucket, lock] = getBucket(key);
        auto const& index = bucket.get().container.template get<0>();

        auto it = index.lower_bound(key);

        task::ValueAwaitable<SeekIterator> output({});
        auto& seekIt = output.value();
        seekIt.m_it = it;
        seekIt.m_end = index.end();
        seekIt.m_bucketLock = std::move(lock);
        return output;
    }

    task::ValueAwaitable<void> write(
        RANGES::input_range auto&& keys, RANGES::input_range auto&& values)
    {
        for (auto&& [key, value] : RANGES::zip_view(
                 std::forward<decltype(keys)>(keys), std::forward<decltype(values)>(values)))
        {
            auto [bucket, lock] = getBucket(key);
            auto const& index = bucket.get().container.template get<0>();

            std::conditional_t<withMRU, int64_t, Empty> updatedCapacity;
            if constexpr (withMRU)
            {
                updatedCapacity = getSize(value);
            }

            auto it = index.lower_bound(key);
            if (it != index.end() && it->key == key)
            {
                auto& existsValue = it->value;
                if constexpr (withMRU)
                {
                    updatedCapacity -= getSize(existsValue);
                }

                auto&& newValue = value;
                bucket.get().container.modify(
                    it, [&newValue](Data& data) { data.value = std::move(newValue); });
            }
            else
            {
                it = bucket.get().container.emplace_hint(it, Data{.key = key, .value = value});
            }

            if constexpr (withMRU)
            {
                bucket.get().capacity += updatedCapacity;
                updateMRUAndCheck(bucket.get(), it);
            }
        }

        return {};
    }

    task::Task<void> remove(RANGES::input_range auto const& keys)
    {
        for (auto&& key : keys)
        {
            auto [bucket, lock] = getBucket(key);
            auto const& index = bucket.get().container.template get<0>();

            auto it = index.find(key);
            if (it != index.end())
            {
                auto& existsValue = it->value;
                if constexpr (withMRU)
                {
                    bucket.get().capacity -= getSize(existsValue);
                }
                bucket.get().container.erase(it);
            }
        }

        co_return;
    }
};

}  // namespace bcos::storage2::concurrent_ordered_storage