#pragma once

#include "Storage.h"
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/key.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/throw_exception.hpp>
#include <range/v3/view/transform.hpp>
#include <set>

namespace bcos::storage2::concurrent_storage
{

template <bool withMRU, class Key, class Value, class KeyHasher = std::hash<Key>>
class ConcurrentOrderedStorage
  : public storage2::StorageBase<ConcurrentOrderedStorage<withMRU, Key, Value, KeyHasher>, Key,
        Value>
{
private:
    constexpr static int64_t DEFAULT_CAPACITY = 32L * 1024 * 1024;

    struct Data
    {
        Key key;
        Value entry;
    };

    using OrderedContainer = boost::multi_index_container<Data,
        boost::multi_index::indexed_by<boost::multi_index::ordered_unique<boost::multi_index::
                const_mem_fun<Data, std::tuple<std::string_view, std::string_view>, &Data::view>>>>;
    using LRUOrderedContainer = boost::multi_index_container<Data,
        boost::multi_index::indexed_by<
            boost::multi_index::ordered_unique<boost::multi_index::const_mem_fun<Data,
                std::tuple<std::string_view, std::string_view>, &Data::view>>,
            boost::multi_index::sequenced<>>>;
    using Container = std::conditional_t<withMRU, LRUOrderedContainer, OrderedContainer>;

    struct Bucket
    {
        Container container;
        int32_t capacity = 0;

        std::mutex mutex;
    };

    int64_t m_maxCapacity = DEFAULT_CAPACITY;
    std::vector<Bucket> m_buckets;

    std::tuple<Bucket*, std::unique_lock<std::mutex>> getBucket(auto const& key)
    {
        constexpr static KeyHasher keyHasher;

        auto hash = keyHasher(key);
        auto index = hash % m_buckets.size();

        auto& bucket = m_buckets[index];
        return std::make_tuple(&bucket, std::unique_lock<std::mutex>(bucket.mutex));
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
            auto& item = index.front();
            bucket.capacity -= item.entry.size();

            index.pop_front();
            ++clearCount;
        }
    }

public:
    ConcurrentOrderedStorage() : m_buckets(std::thread::hardware_concurrency()) {}

    class Iterator
    {
    public:
        task::Task<bool> next() { co_return (++m_it) != m_end; }
        task::Task<Key> key() const { co_return m_it->first; }
        task::Task<Value> value() const { co_return &(m_it->second); }

    private:
        typename Container::iterator m_it;
        typename Container::iterator m_end;
        std::unique_lock<std::mutex> m_lock;
    };

    task::Task<void> impl_read(RANGES::input_range auto const& keys,
        RANGES::output_range<std::optional<Value>> auto&& outputs)
    {
        for (auto&& [key, output] : RANGES::zip_view(keys, outputs))
        {
            auto [bucket, lock] = getBucket(key);
            auto const& index = bucket->container.template get<0>();

            auto it = index.find(key);
            if (it != index.end())
            {
                output.emplace(it->entry);
                if constexpr (withMRU)
                {
                    updateMRUAndCheck(*bucket, it);
                }
            }
        }

        co_return;
    }

    task::Task<Iterator> impl_seek(auto const& key)
    {
        auto [bucket, lock] = getBucket(key);
        auto const& index = bucket->container.template get<0>();

        auto it = index.lower_bound(key);
        co_return {--it, index.end(), std::move(lock)};
    }

    task::Task<void> import() { co_return; }
};

}  // namespace bcos::storage2::concurrent_storage