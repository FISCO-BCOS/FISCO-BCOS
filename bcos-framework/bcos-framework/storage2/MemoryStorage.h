#pragma once

#include "Storage.h"
#include "bcos-task/AwaitableValue.h"
#include "bcos-utilities/Overloaded.h"
#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/rw_mutex.h>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/key.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/throw_exception.hpp>
#include <range/v3/view/chunk_by.hpp>
#include <range/v3/view/transform.hpp>
#include <range/v3/view/zip.hpp>
#include <type_traits>
#include <utility>

namespace bcos::storage2::memory_storage
{

struct NullLock
{
    NullLock(auto&&... /*unused*/) {}
    constexpr bool try_acquire(auto&&... /*unused*/) { return true; }
    constexpr void release() {}
    constexpr bool upgrade_to_writer() { return true; }
};
struct NOT_EXISTS_TYPE
{
};

template <class Object>
concept HasMemberSize = requires(Object object) {
    { object.size() } -> std::integral;
};

using Empty = std::monostate;

enum Attribute : uint8_t
{
    UNORDERED = 0,
    ORDERED = 1,
    CONCURRENT = 1 << 1,
    LRU = 1 << 2,
    LOGICAL_DELETION = 1 << 3
};

inline int64_t getSize(auto const& object)
{
    using ObjectType = std::remove_cvref_t<decltype(object)>;
    if constexpr (HasMemberSize<ObjectType>)
    {
        return object.size();
    }
    // Treat any no-size() object as trivial, TODO: fix it
    return sizeof(ObjectType);
}

constexpr inline struct DeleteItem
{
} deleteItem{};

template <class KeyType, class ValueType = Empty, int attribute = Attribute::UNORDERED,
    class HasherType = std::hash<KeyType>, class Equal = std::equal_to<>,
    class BucketHasherType = HasherType>
class MemoryStorage
{
public:
    constexpr static bool isMemoryStorage = true;

    constexpr static bool withOrdered = (attribute & Attribute::ORDERED) != 0;
    constexpr static bool withConcurrent = (attribute & Attribute::CONCURRENT) != 0;
    constexpr static bool withLRU = (attribute & Attribute::LRU) != 0;
    constexpr static bool withLogicalDeletion = (attribute & Attribute::LOGICAL_DELETION) != 0;

    using Key = KeyType;
    using Value = ValueType;
    using BucketHasher = BucketHasherType;

    MemoryStorage(unsigned buckets = 0, int64_t capacity = DEFAULT_CAPACITY)
    {
        if constexpr (withConcurrent)
        {
            m_buckets = decltype(m_buckets)(buckets == 0 ? getBucketSize() : buckets);
        }
        if constexpr (withLRU)
        {
            m_maxCapacity = capacity;
        }
    }
    MemoryStorage(const MemoryStorage&) = default;
    MemoryStorage(MemoryStorage&&) noexcept = default;
    MemoryStorage& operator=(const MemoryStorage&) = default;
    MemoryStorage& operator=(MemoryStorage&&) noexcept = default;
    ~MemoryStorage() noexcept = default;

    constexpr unsigned getBucketSize()
    {
        return withConcurrent ? std::thread::hardware_concurrency() * 2 : 1;  // Magic
    }

    static_assert(withOrdered || !std::is_void_v<HasherType>);
    static_assert(!withConcurrent || !std::is_void_v<BucketHasherType>);

    constexpr static unsigned DEFAULT_CAPACITY = 32 * 1024 * 1024;  // For mru
    using Mutex = std::conditional_t<withConcurrent, tbb::rw_mutex, Empty>;
    using Lock = std::conditional_t<withConcurrent, tbb::rw_mutex::scoped_lock, NullLock>;
    using DataValue = std::conditional_t<withLogicalDeletion, std::optional<ValueType>, ValueType>;

    struct Data
    {
        KeyType key;
        [[no_unique_address]] DataValue value;
    };

    using IndexType = std::conditional_t<withOrdered,
        boost::multi_index::ordered_unique<boost::multi_index::member<Data, KeyType, &Data::key>,
            std::less<>>,
        boost::multi_index::hashed_unique<boost::multi_index::member<Data, KeyType, &Data::key>,
            HasherType, Equal>>;
    using Container = std::conditional_t<withLRU,
        boost::multi_index_container<Data,
            boost::multi_index::indexed_by<IndexType, boost::multi_index::sequenced<>>>,
        boost::multi_index_container<Data, boost::multi_index::indexed_by<IndexType>>>;
    struct Bucket
    {
        Container container;
        [[no_unique_address]] Mutex mutex;  // For concurrent
        [[no_unique_address]] std::conditional_t<withLRU, int64_t, Empty> capacity = {};  // LRU
    };
    using Buckets = std::conditional_t<withConcurrent, std::vector<Bucket>, std::array<Bucket, 1>>;

    Buckets m_buckets;
    [[no_unique_address]] std::conditional_t<withLRU, int64_t, Empty> m_maxCapacity;

    friend void setMaxCapacity(MemoryStorage& storage, int64_t capacity)
        requires withLRU
    {
        storage.m_maxCapacity = capacity;
    }

    friend size_t getBucketIndex(MemoryStorage const& storage, auto const& key)
    {
        if constexpr (!withConcurrent)
        {
            return 0;
        }
        else
        {
            auto hash = typename MemoryStorage::BucketHasher{}(key);
            return hash % storage.m_buckets.size();
        }
    }

    friend Bucket& getBucket(MemoryStorage& storage, auto const& key) noexcept
    {
        if constexpr (!withConcurrent)
        {
            return storage.m_buckets[0];
        }
        else
        {
            auto index = getBucketIndex(storage, key);
            return storage.m_buckets[index];
        }
    }

    void updateLRUAndCheck(
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

    friend auto tag_invoke(bcos::storage2::tag_t<readSome> /*unused*/, MemoryStorage& storage,
        ::ranges::input_range auto keys) -> task::AwaitableValue<std::vector<std::optional<Value>>>
    {
        task::AwaitableValue<std::vector<std::optional<Value>>> result;
        if constexpr (::ranges::sized_range<decltype(keys)>)
        {
            result.value().reserve(::ranges::size(keys));
        }

        for (auto&& key : keys)
        {
            auto& bucket = getBucket(storage, key);
            Lock lock(bucket.mutex, false);

            auto const& index = bucket.container.template get<0>();
            if (auto it = index.find(key); it != index.end())
            {
                result.value().emplace_back(it->value);
                if constexpr (withLRU)
                {
                    if (lock.upgrade_to_writer())
                    {
                        storage.updateLRUAndCheck(bucket, it);
                    }
                }
            }
            else
            {
                result.value().emplace_back(std::optional<Value>{});
            }
        }
        return result;
    }

    friend task::AwaitableValue<std::optional<Value>> tag_invoke(
        storage2::tag_t<storage2::readOne> /*unused*/, MemoryStorage& storage, auto key,
        auto&&... args)
    {
        auto result = storage.readOne(std::move(key), std::forward<decltype(args)>(args)...);
        return {std::visit(
            bcos::overloaded{[](std::optional<Value>& value) { return std::move(value); },
                [](NOT_EXISTS_TYPE) { return std::optional<Value>{}; }},
            result.value())};
    }

    auto readOne(auto key, auto&&... /*unused*/)
    {
        task::AwaitableValue<std::variant<std::optional<Value>, NOT_EXISTS_TYPE>> result;
        auto& bucket = getBucket(*this, key);
        Lock lock(bucket.mutex, false);

        auto const& index = bucket.container.template get<0>();
        if (auto it = index.find(key); it != index.end())
        {
            result.value().template emplace<std::optional<Value>>(it->value);

            if constexpr (withLRU)
            {
                if (lock.upgrade_to_writer())
                {
                    updateLRUAndCheck(bucket, it);
                }
            }
        }
        else
        {
            result.value().template emplace<NOT_EXISTS_TYPE>();
        }
        return result;
    }

    void writeOne(Bucket& bucket, auto key, auto value, bool direct)
    {
        auto const& index = bucket.container.template get<0>();
        std::conditional_t<withLRU, int64_t, Empty> updatedCapacity{};
        constexpr static auto deleteOP = std::is_same_v<std::decay_t<decltype(value)>, DeleteItem>;
        if constexpr (withLRU)
        {
            updatedCapacity = getSize(key) + getSize(value);
        }

        auto it = index.find(key);
        if (it != index.end())
        {
            auto& existsValue = it->value;
            if constexpr (deleteOP)
            {
                if constexpr (withLRU)
                {
                    updatedCapacity = -updatedCapacity;
                }
                if constexpr (withLogicalDeletion)
                {
                    if (existsValue)
                    {
                        if (!direct)
                        {
                            bucket.container.modify(
                                it, [](Data& data) mutable { data.value.reset(); });
                        }
                        else
                        {
                            bucket.container.erase(it);
                        }
                    }
                }
                else
                {
                    bucket.container.erase(it);
                }
            }
            else
            {
                if constexpr (withLRU)
                {
                    updatedCapacity -= (getSize(key) + getSize(existsValue));
                }
                bucket.container.modify(
                    it, [&](Data& data) mutable { data.value = std::move(value); });
            }
        }
        else
        {
            if constexpr (deleteOP)
            {
                if constexpr (withLRU)
                {
                    updatedCapacity = 0;
                }
                if constexpr (withLogicalDeletion)
                {
                    if constexpr (std::is_same_v<std::decay_t<decltype(key)>, Key>)
                    {
                        it = bucket.container.emplace_hint(
                            it, Data{.key = std::move(key), .value = {}});
                    }
                    else
                    {
                        it = bucket.container.emplace_hint(
                            it, Data{.key = Key{std::move(key)}, .value = {}});
                    }
                }
            }
            else
            {
                it = bucket.container.emplace_hint(
                    it, typename MemoryStorage::Data{
                            .key = Key(std::move(key)), .value = std::move(value)});
            }
        }

        if constexpr (!deleteOP && withLRU)
        {
            bucket.capacity += updatedCapacity;
            updateLRUAndCheck(bucket, it);
        }
    }

    friend task::AwaitableValue<void> tag_invoke(storage2::tag_t<storage2::writeOne> /*unused*/,
        MemoryStorage& storage, auto key, auto value)
    {
        auto& bucket = getBucket(storage, key);
        Lock lock(bucket.mutex, true);
        storage.writeOne(bucket, std::move(key), std::move(value), false);
        return {};
    }

    friend task::AwaitableValue<void> tag_invoke(storage2::tag_t<storage2::writeSome> /*unused*/,
        MemoryStorage& storage, ::ranges::input_range auto keyValues)
    {
        for (auto&& [key, value] : keyValues)
        {
            auto& bucket = getBucket(storage, key);
            Lock lock(bucket.mutex, true);
            storage.writeOne(bucket, std::forward<decltype(key)>(key),
                std::forward<decltype(value)>(value), false);
        }

        return {};
    }

    task::AwaitableValue<void> removeSome(::ranges::input_range auto keys, bool direct)
    {
        for (auto&& key : keys)
        {
            auto& bucket = getBucket(*this, key);
            Lock lock(bucket.mutex, true);
            writeOne(bucket, std::forward<decltype(key)>(key), deleteItem, direct);
        }

        return {};
    }

    friend task::AwaitableValue<void> tag_invoke(storage2::tag_t<storage2::removeSome> /*unused*/,
        MemoryStorage& storage, ::ranges::input_range auto keys)
    {
        return storage.removeSome(std::move(keys), false);
    }

    friend task::AwaitableValue<void> tag_invoke(storage2::tag_t<storage2::removeSome> /*unused*/,
        MemoryStorage& storage, ::ranges::input_range auto keys, DIRECT_TYPE /*unused*/)
    {
        return storage.removeSome(std::move(keys), true);
    }

    friend task::AwaitableValue<void> tag_invoke(
        storage2::tag_t<merge> /*unused*/, MemoryStorage& toStorage, MemoryStorage& fromStorage)
        requires(!std::is_const_v<decltype(fromStorage)>)
    {
        for (auto&& [bucket, fromBucket] :
            ::ranges::views::zip(toStorage.m_buckets, fromStorage.m_buckets))
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
    class Iterator
    {
    private:
        size_t m_bucketIndex{};
        std::reference_wrapper<Buckets> m_buckets;
        std::unique_ptr<Lock> m_bucketLock;
        ::ranges::iterator_t<Container> m_begin;
        ::ranges::iterator_t<Container> m_end;

        using IteratorValue = std::conditional_t<withLogicalDeletion, const Value*, const Value&>;

        void updateIterator()
        {
            if constexpr (withConcurrent)
            {
                m_bucketLock = std::make_unique<Lock>(m_buckets.get()[m_bucketIndex].mutex, false);
            }
            m_begin = (m_buckets.get()[m_bucketIndex]).container.begin();
            m_end = (m_buckets.get()[m_bucketIndex]).container.end();
        }

    public:
        Iterator(Buckets& buckets) : m_buckets(buckets) { updateIterator(); }

        auto next()
        {
            std::optional<std::tuple<Key const&, IteratorValue>> result;
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
                return task::AwaitableValue(std::move(result));
            }

            if (m_bucketIndex + 1 < m_buckets.get().size())
            {
                ++m_bucketIndex;
                updateIterator();
                return next();
            }
            return task::AwaitableValue(std::move(result));
        }

        auto seek(const auto& key)
            requires(!withConcurrent && withOrdered)
        {
            auto const& index = m_buckets.get()[m_bucketIndex].container.template get<0>();
            m_begin = index.lower_bound(std::forward<decltype(key)>(key));
        }
    };

    friend auto tag_invoke(
        bcos::storage2::tag_t<storage2::range> /*unused*/, MemoryStorage& storage)
    {
        return task::AwaitableValue(Iterator(storage.m_buckets));
    }

    friend auto tag_invoke(bcos::storage2::tag_t<storage2::range> /*unused*/,
        MemoryStorage& storage, RANGE_SEEK_TYPE /*unused*/, const auto& key)
        requires(!withConcurrent && withOrdered)
    {
        auto iterator = Iterator(storage.m_buckets);
        iterator.seek(key);
        return task::AwaitableValue(std::move(iterator));
    }

    template <class FromStorage>
        requires withConcurrent
    friend task::Task<void> tag_invoke(
        storage2::tag_t<merge> /*unused*/, MemoryStorage& toStorage, FromStorage& fromStorage)
    {
        auto& bucket = fromStorage.m_buckets[0];
        auto& index = bucket.container.template get<0>();
        auto sortedList = ::ranges::views::transform(index,
                              [&](typename std::decay_t<FromStorage>::Data const& data) {
                                  return std::make_tuple(
                                      std::addressof(data), getBucketIndex(toStorage, data.key));
                              }) |
                          ::ranges::to<std::vector>();
        std::sort(sortedList.begin(), sortedList.end(), [](auto const& left, auto const& right) {
            return std::get<1>(left) < std::get<1>(right);
        });
        auto chunks = ::ranges::views::chunk_by(sortedList, [](auto const& left,
                                                                auto const& right) {
            return std::get<1>(left) == std::get<1>(right);
        }) | ::ranges::to<std::vector>();
        tbb::parallel_for(tbb::blocked_range<size_t>(0U, chunks.size()), [&](auto const& range) {
            for (auto i = range.begin(); i < range.end(); ++i)
            {
                auto& chunk = chunks[i];
                auto index = std::get<1>(chunk.front());
                auto& bucket = toStorage.m_buckets[index];
                Lock lock(bucket.mutex, true);

                for (auto& [data, _] : chunk)
                {
                    auto&& [key, value] = *data;
                    if constexpr (std::decay_t<FromStorage>::withLogicalDeletion)
                    {
                        if (value)
                        {
                            toStorage.writeOne(bucket, key, *value, true);
                        }
                        else
                        {
                            toStorage.writeOne(bucket, key, deleteItem, true);
                        }
                    }
                    else
                    {
                        toStorage.writeOne(bucket, *key, *value, false);
                    }
                }
            }
        });
        co_return;
    }
};

}  // namespace bcos::storage2::memory_storage
