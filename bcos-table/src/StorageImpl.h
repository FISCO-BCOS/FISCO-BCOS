#pragma once

#include "bcos-concepts/Basic.h"
#include "bcos-concepts/ByteBuffer.h"
#include <bcos-concepts/storage/Storage2.h>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/key.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/throw_exception.hpp>

namespace bcos::storage
{

// TODO: Only for demo now
template <bool withMRU>
class StorageImpl : public concepts::storage::StorageBase<StorageImpl<withMRU>>
{
public:
    task::Task<void> impl_getRows(concepts::bytebuffer::ByteBuffer auto const& tableName,
        concepts::storage::Keys auto const& keys, concepts::storage::Entries auto& entriesOut)
    {
        concepts::resizeTo(entriesOut, RANGES::size(keys));
        auto [bucket, lock] = getBucket(tableName);
        auto& index = bucket->container.template get<0>();

        for (auto const& [keyIt, outIt] :
            RANGES::zip_view<decltype(keys), decltype(entriesOut)>(keys, entriesOut))
        {
            auto keyView = concepts::bytebuffer::toView(keyIt);
            auto entryIt = index.find(std::make_tuple(tableName, keyView));
            if (entryIt != index.end() && entryIt->entry.status() != Entry::DELETED)
            {
                if constexpr (withMRU)
                {
                    updateMRUAndCheck(*bucket, entryIt);
                }
                outIt = entryIt->entry;
            }
        }
    }

    task::Task<void> impl_setRows(concepts::bytebuffer::ByteBuffer auto const& tableName,
        concepts::storage::Keys auto const& keys, concepts::storage::Entries auto const& entries)
    {
        if (RANGES::size(keys) != RANGES::size(entries))
        {
            BOOST_THROW_EXCEPTION(1);
        }

        auto [bucket, lock] = getBucket(tableName);
        auto& tableNames = bucket->tableNames;
        auto& index = bucket->container.template get<0>();

        auto intputTablNameView = concepts::bytebuffer::toView(tableName);
        std::string_view tableNameView;
        auto tableNameIt = tableNames.find(intputTablNameView);
        if (tableNameIt == tableNames.end())
        {
            tableNameIt = tableNames.emplace(std::string(intputTablNameView));
        }
        tableNameView = *tableNameIt;

        for (auto const& [keyIt, valueIt] :
            RANGES::zip_view<decltype(keys), decltype(entries)>(keys, entries))
        {
            auto keyView = concepts::bytebuffer::toView(keyIt);
            auto entryIt = index.find(std::make_tuple(tableName, keyView));

            if (entryIt != index.end())
            {
                auto& value = *valueIt;
                bucket->container.modify(entryIt, [&value](Data& data) { data.entry = value; });

                if constexpr (withMRU)
                {
                    updateMRUAndCheck(*bucket, entryIt);
                }
            }
        }
    }

private:
    constexpr static int64_t DEFAULT_CAPACITY = 32L * 1024 * 1024;
    int64_t m_maxCapacity = DEFAULT_CAPACITY;

    struct Data
    {
        std::string_view tableName;
        std::string key;
        Entry entry;

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
        std::set<std::string> tableNames;
        Container container;
        int32_t capacity = 0;

        std::mutex mutex;
    };
    std::vector<Bucket> m_buckets;

    std::tuple<Bucket*, std::unique_lock<std::mutex>> getBucket(
        concepts::bytebuffer::ByteBuffer auto const& tableName)
    {
        constexpr static std::hash<std::string_view> hasher;

        auto hash = hasher(toView(tableName));
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

}  // namespace bcos::storage