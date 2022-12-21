#pragma once

#include "bcos-concepts/Basic.h"
#include "bcos-concepts/ByteBuffer.h"
#include <bcos-framework/storage2/Storage2.h>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/key.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/throw_exception.hpp>
#include <range/v3/view/transform.hpp>

namespace bcos::storage2
{

template <bool withMRU, class Entry>
class StorageImpl : public storage2::Storage2Base<StorageImpl<withMRU, Entry>, Entry>
{
public:
    StorageImpl() : m_buckets(std::thread::hardware_concurrency()) {}

    task::Task<void> impl_getRows(
        std::string_view tableName, InputKeys auto const& keys, OutputEntries auto& out)
    {
        concepts::resizeTo(out, RANGES::size(keys));
        auto [bucket, lock] = getBucket(tableName);
        auto& index = bucket->container.template get<0>();

        for (auto const& [key, value] : RANGES::zip_view(keys, out))
        {
            auto keyView = concepts::bytebuffer::toView(key);
            auto entryIt = index.find(std::make_tuple(tableName, keyView));
            if (entryIt != index.end())
            {
                value.emplace(entryIt->entry);
                if constexpr (withMRU)
                {
                    updateMRUAndCheck(*bucket, entryIt);
                }
            }
        }

        co_return;
    }

    task::Task<void> impl_setRows(
        std::string_view tableName, InputKeys auto const& keys, InputEntries auto const& entries)
    {
        auto [bucket, lock] = getBucket(tableName);
        auto& tableNames = bucket->tableNames;
        auto& index = bucket->container.template get<0>();

        auto intputTablNameView = concepts::bytebuffer::toView(tableName);
        auto tableNameIt = tableNames.lower_bound(intputTablNameView);
        if (tableNameIt == tableNames.end() || *tableNameIt != intputTablNameView)
        {
            tableNameIt = tableNames.emplace_hint(tableNameIt, std::string(intputTablNameView));
        }
        auto tableNameView = *tableNameIt;

        for (auto const& [keyIt, valueIt] : RANGES::zip_view(keys, entries))
        {
            auto keyView = concepts::bytebuffer::toView(keyIt);
            auto combineKey = std::make_tuple(tableName, keyView);
            auto entryIt = index.lower_bound(combineKey);

            if (entryIt == index.end() || entryIt->view() != combineKey)
            {
                entryIt = index.emplace_hint(entryIt,
                    Data{.tableName = tableNameView, .key = std::string(keyView), .entry = {}});
            }
            auto& value = valueIt;
            bucket->container.modify(entryIt, [&value](Data& data) { data.entry = value; });
        }

        co_return;
    }

private:
    constexpr static int64_t DEFAULT_CAPACITY = 32L * 1024 * 1024;
    int64_t m_maxCapacity = DEFAULT_CAPACITY;

    struct Data
    {
        std::string_view tableName;
        std::string key;
        storage::Entry entry;

        std::tuple<std::string_view, std::string_view> view() const
        {
            return std::make_tuple(tableName, std::string_view(key));
        }
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
        std::set<std::string, std::less<>> tableNames;
        Container container;
        int32_t capacity = 0;

        std::mutex mutex;
    };
    std::vector<Bucket> m_buckets;

    std::tuple<Bucket*, std::unique_lock<std::mutex>> getBucket(
        concepts::bytebuffer::ByteBuffer auto const& tableName)
    {
        constexpr static std::hash<std::string_view> hasher;

        auto hash = hasher(concepts::bytebuffer::toView(tableName));
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

        if (clearCount > 0)
        {
            STORAGE_LOG(TRACE) << "LRUStorage cleared:" << clearCount
                               << ", current size: " << bucket.container.size();
        }
    }
};

}  // namespace bcos::storage2