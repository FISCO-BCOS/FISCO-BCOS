#pragma once

#include "Storage.h"
#include "bcos-task/AwaitableValue.h"
#include <bcos-utilities/NullLock.h>
#include <oneapi/tbb/parallel_for_each.h>
#include <boost/container/small_vector.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/key.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/throw_exception.hpp>
#include <functional>
#include <mutex>
#include <range/v3/view/transform.hpp>
#include <thread>
#include <type_traits>
#include <utility>

namespace bcos::storage2::memory_storage
{

template <class Object>
concept HasMemberSize = requires(Object object) {
                            // clang-format off
    { object.size() } -> std::integral;
                            // clang-format on
                        };

struct Empty
{
};
struct Deleted
{
};

enum Attribute : int
{
    NONE = 0,
    ORDERED = 1,
    CONCURRENT = 2,
    MRU = 4,
    LOGICAL_DELETION = 8,
};

template <class KeyType, class ValueType = Empty, Attribute attribute = Attribute::NONE,
    class BucketHasher = void>
class MemoryStorage
{
private:
    constexpr static bool withOrdered = (attribute & Attribute::ORDERED) != 0;
    constexpr static bool withConcurrent = (attribute & Attribute::CONCURRENT) != 0;
    constexpr static bool withMRU = (attribute & Attribute::MRU) != 0;
    constexpr static bool withLogicalDeletion = (attribute & Attribute::LOGICAL_DELETION) != 0;

    constexpr static unsigned BUCKETS_COUNT = 64;  // Magic number 64
    constexpr unsigned getBucketSize() { return withConcurrent ? BUCKETS_COUNT : 1; }
    constexpr static int MOSTLY_CACHELINE_SIZE = 64;

    static_assert(!withConcurrent || !std::is_void_v<BucketHasher>);

    constexpr static unsigned DEFAULT_CAPACITY = 4 * 1024 * 1024;  // For mru
    using Mutex = std::mutex;
    using Lock = std::conditional_t<withConcurrent, std::unique_lock<Mutex>, utilities::NullLock>;
    using BucketMutex = std::conditional_t<withConcurrent, Mutex, Empty>;
    using DataValueType =
        std::conditional_t<withLogicalDeletion, std::variant<Deleted, ValueType>, ValueType>;
    struct Data
    {
        KeyType key;
        [[no_unique_address]] DataValueType value;
    };

    using IndexType = std::conditional_t<withOrdered,
        boost::multi_index::ordered_unique<boost::multi_index::member<Data, KeyType, &Data::key>>,
        boost::multi_index::hashed_unique<boost::multi_index::member<Data, KeyType, &Data::key>>>;
    using Container = std::conditional_t<withMRU,
        boost::multi_index_container<Data,
            boost::multi_index::indexed_by<IndexType, boost::multi_index::sequenced<>>>,
        boost::multi_index_container<Data, boost::multi_index::indexed_by<IndexType>>>;
    struct alignas(MOSTLY_CACHELINE_SIZE) Bucket
    {
        Container container;
        [[no_unique_address]] BucketMutex mutex;  // For concurrent
        [[no_unique_address]] std::conditional_t<withMRU, int64_t, Empty> capacity = {};  // For mru
    };
    using Buckets = std::conditional_t<withConcurrent, std::vector<Bucket>, std::array<Bucket, 1>>;

    Buckets m_buckets;
    [[no_unique_address]] std::conditional_t<withMRU, int64_t, Empty> m_maxCapacity;

    std::tuple<std::reference_wrapper<Bucket>, Lock> getBucket(auto const& key)
    {
        if constexpr (!withConcurrent)
        {
            return std::make_tuple(std::ref(m_buckets[0]), Lock(Empty{}));
        }
        auto index = getBucketIndex(key);

        auto& bucket = m_buckets[index];
        return std::make_tuple(std::ref(bucket), Lock(bucket.mutex));
    }

    Bucket& getBucketByIndex(size_t index) { return m_buckets[index]; }

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
        requires withMRU
    {
        auto& index = bucket.container.template get<1>();
        auto seqIt = index.iterator_to(*entryIt);
        index.relocate(index.end(), seqIt);

        while (bucket.capacity > m_maxCapacity && !bucket.container.empty())
        {
            auto const& item = index.front();
            bucket.capacity -= getSize(item.value);

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

    MemoryStorage()
        requires(!withConcurrent)
    {
        if constexpr (withMRU)
        {
            m_maxCapacity = DEFAULT_CAPACITY;
        }
    }

    explicit MemoryStorage(unsigned buckets = BUCKETS_COUNT)
        requires(withConcurrent)
      : m_buckets(std::min(buckets, getBucketSize()))
    {
        if constexpr (withMRU)
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
        requires withMRU
    {
        m_maxCapacity = capacity;
    }

    class ReadIterator
    {
    private:
        int64_t m_index = -1;
        boost::container::small_vector<const Data*, 1> m_iterators;
        [[no_unique_address]] std::conditional_t<withConcurrent,
            boost::container::small_vector<Lock, 1>, Empty>
            m_bucketLocks;

    public:
        friend class MemoryStorage;
        using Key = const KeyType&;
        using Value = const ValueType&;

        ReadIterator() = default;
        ReadIterator(const ReadIterator&) = delete;
        ReadIterator(ReadIterator&&) noexcept = default;
        ReadIterator& operator=(const ReadIterator&) = delete;
        ReadIterator& operator=(ReadIterator&&) noexcept = default;
        ~ReadIterator() noexcept = default;

        task::AwaitableValue<bool> next() &
        {
            return {static_cast<size_t>(++m_index) != m_iterators.size()};
        }
        task::AwaitableValue<Key> key() const& { return {m_iterators[m_index]->key}; }
        task::AwaitableValue<Value> value() const&
        {
            if constexpr (withLogicalDeletion)
            {
                return {std::get<ValueType>(m_iterators[m_index]->value)};
            }
            else
            {
                return {m_iterators[m_index]->value};
            }
        }
        task::AwaitableValue<bool> hasValue() const
        {
            auto* data = m_iterators[m_index];
            if constexpr (withLogicalDeletion)
            {
                if (data != nullptr && std::holds_alternative<Deleted>(data->value))
                {
                    return false;
                }
            }
            return {data != nullptr};
        }

        void release()
        {
            if constexpr (withConcurrent)
            {
                m_bucketLocks.clear();
            }
        }

        auto range() const&
        {
            return m_iterators |
                   RANGES::views::transform(
                       [](auto const* data) -> std::tuple<const KeyType*, const ValueType*> {
                           if (!data)
                           {
                               return {nullptr, nullptr};
                           }
                           return {std::addressof(data->key), std::addressof(data->value)};
                       });
        }
    };

    class SeekIterator
    {
    private:
        typename Container::iterator m_it;
        typename Container::iterator m_end;
        [[no_unique_address]] Lock m_bucketLock;
        bool m_started = false;

    public:
        friend class MemoryStorage;
        using Key = const KeyType&;
        using Value = const ValueType&;

        task::AwaitableValue<bool> next() &
        {
            if (!m_started)
            {
                m_started = true;
                return {m_it != m_end};
            }
            return {(++m_it) != m_end};
        }
        task::AwaitableValue<Key> key() const { return {m_it->key}; }
        task::AwaitableValue<Value> value() const
        {
            if constexpr (withLogicalDeletion)
            {
                return {std::get<ValueType>(m_it->value)};
            }
            else
            {
                return {m_it->value};
            }
        }
        task::AwaitableValue<bool> hasValue() const
        {
            if constexpr (withLogicalDeletion)
            {
                if (std::holds_alternative<Deleted>(m_it->value))
                {
                    return false;
                }
            }
            return true;
        }

        void release() { m_bucketLock.unlock(); }

        auto range() const&
        {
            return RANGES::subrange<decltype(m_it), decltype(m_end)>(m_it, m_end) |
                   RANGES::views::transform(
                       [](auto const& it) -> std::tuple<const KeyType*, const ValueType*> {
                           if constexpr (withLogicalDeletion)
                           {
                               if (std::holds_alternative<Deleted>(it.value))
                               {
                                   return {std::addressof(it.key), nullptr};
                               }
                               return {std::addressof(it.key),
                                   std::addressof(std::get<ValueType>(it.value))};
                           }
                           else
                           {
                               return {std::addressof(it.key), std::addressof(it.value)};
                           }
                       });
        }
    };

    task::AwaitableValue<ReadIterator> read(RANGES::input_range auto&& keys)
    {
        task::AwaitableValue<ReadIterator> outputAwaitable(ReadIterator{});
        ReadIterator& output = outputAwaitable.value();
        if constexpr (RANGES::sized_range<std::remove_cvref_t<decltype(keys)>>)
        {
            output.m_iterators.reserve(RANGES::size(keys));
        }

        std::conditional_t<withConcurrent, std::bitset<BUCKETS_COUNT>, Empty> locks;
        for (auto&& key : keys)
        {
            auto bucketIndex = getBucketIndex(key);
            auto& bucket = getBucketByIndex(bucketIndex);

            if constexpr (withConcurrent)
            {
                if (!locks[bucketIndex])
                {
                    locks[bucketIndex] = true;
                    output.m_bucketLocks.emplace_back(bucket.mutex);
                }
            }

            auto const& index = bucket.container.template get<0>();
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

        return outputAwaitable;
    }

    task::AwaitableValue<SeekIterator> seek(auto const& key)
        requires(withOrdered)
    {
        auto [bucket, lock] = getBucket(key);
        auto const& index = bucket.get().container.template get<0>();

        decltype(index.begin()) it;
        if constexpr (std::is_same_v<storage2::STORAGE_BEGIN_TYPE,
                          std::remove_cvref_t<decltype(key)>>)
        {
            it = index.begin();
        }
        else
        {
            it = index.lower_bound(key);
        }

        task::AwaitableValue<SeekIterator> output({});
        auto& seekIt = output.value();
        seekIt.m_it = it;
        seekIt.m_end = index.end();
        if constexpr (withConcurrent)
        {
            seekIt.m_bucketLock = std::move(lock);
        }

        return output;
    }

    task::AwaitableValue<void> write(
        RANGES::input_range auto&& keys, RANGES::input_range auto&& values)
    {
        for (auto&& [key, value] : RANGES::views::zip(
                 std::forward<decltype(keys)>(keys), std::forward<decltype(values)>(values)))
        {
            auto [bucket, lock] = getBucket(key);
            auto const& index = bucket.get().container.template get<0>();

            std::conditional_t<withMRU, int64_t, Empty> updatedCapacity;
            if constexpr (withMRU)
            {
                updatedCapacity = getSize(value);
            }

            typename Container::iterator it;
            if constexpr (withOrdered)
            {
                it = index.lower_bound(key);
            }
            else
            {
                it = index.find(key);
            }
            if (it != index.end() && it->key == key)
            {
                if constexpr (withMRU)
                {
                    auto& existsValue = it->value;
                    updatedCapacity -= getSize(existsValue);
                }

                bucket.get().container.modify(
                    it, [newValue = std::forward<decltype(value)>(value)](Data& data) mutable {
                        data.value = std::forward<decltype(newValue)>(newValue);
                    });
            }
            else
            {
                it = bucket.get().container.emplace_hint(
                    it, Data{.key = key, .value = std::forward<decltype(value)>(value)});
            }

            if constexpr (withMRU)
            {
                bucket.get().capacity += updatedCapacity;
                updateMRUAndCheck(bucket.get(), it);
            }
        }

        return {};
    }

    task::AwaitableValue<void> remove(RANGES::input_range auto const& keys)
    {
        for (auto&& key : keys)
        {
            auto [bucket, lock] = getBucket(key);
            auto const& index = bucket.get().container.template get<0>();

            auto it = index.find(key);
            if (it != index.end())
            {
                auto& existsValue = it->value;
                if constexpr (withLogicalDeletion)
                {
                    if (std::holds_alternative<Deleted>(existsValue))
                    {
                        // Already deleted
                        return {};
                    }
                }

                if constexpr (withMRU)
                {
                    bucket.get().capacity -= getSize(existsValue);
                }

                if constexpr (withLogicalDeletion)
                {
                    bucket.get().container.modify(it, [](Data& data) mutable {
                        data.value.template emplace<Deleted>(Deleted{});
                    });
                }
                else
                {
                    bucket.get().container.erase(it);
                }
            }
            else
            {
                if constexpr (withLogicalDeletion)
                {
                    it = bucket.get().container.emplace_hint(
                        it, Data{.key = key, .value = Deleted{}});
                }
            }
        }

        return {};
    }

    task::Task<void> merge(MemoryStorage& from)
    {
        for (auto bucketPair : RANGES::views::zip(m_buckets, from.m_buckets))
        {
            auto& [bucket, fromBucket] = bucketPair;
            Lock toLock(bucket.mutex);
            Lock fromLock(fromBucket.mutex);

            auto& index = bucket.container.template get<0>();
            auto& fromIndex = fromBucket.container.template get<0>();

            while (!fromIndex.empty())
            {
                auto [it, merged] = index.merge(fromIndex, fromIndex.begin());
                if (!merged)
                {
                    index.insert(index.erase(it), fromIndex.extract(fromIndex.begin()));
                }
            }
        }
        co_return;
    }

    void swap(MemoryStorage& from)
    {
        for (auto bucketPair : RANGES::views::zip(m_buckets, from.m_buckets))
        {
            auto& [bucket, fromBucket] = bucketPair;
            Lock toLock(bucket.mutex);
            Lock fromLock(fromBucket.mutex);
            bucket.container.swap(fromBucket.container);
        }
    }

    bool empty() const
    {
        bool allEmpty = true;
        for (auto& bucket : m_buckets)
        {
            Lock lock(bucket.mutex);
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