#pragma once

#include "bcos-concepts/Basic.h"
#include <bcos-concepts/storage/Storage.h>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/key.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

namespace bcos::storage
{

template <bool withMRU>
class StorageImpl : public concepts::storage::StorageBase<StorageImpl<withMRU>>
{
public:
    task::Task<void> impl_getRows(concepts::bytebuffer::ByteBuffer auto const& tableName,
        concepts::storage::Keys auto const& keys, concepts::storage::Entries auto& out)
    {
        concepts::resizeTo(out, RANGES::size(keys));

        auto [bucket, lock] = getBucket(tableName);
        boost::ignore_unused(lock);

        auto& index = bucket->container.template get<0>();

        auto outIt = RANGES::begin(out);
        for (auto const& it = RANGES::begin(keys);)
        {
            auto keyView = concepts::bytebuffer::toView(it);
            auto entryIt = index.find(std::make_tuple(tableName, keyView));
            if (entryIt != index.end())
            {
                auto& entry = entryIt->entry;
                if (entry.status() == Entry::DELETED)
                {
                    continue;
                }
            }

            ++outIt;
        }

        auto it = bucket->container.template get<0>().find(std::make_tuple(tableName, keyView));
        if (it != bucket->container.template get<0>().end())
        {
            auto& entry = it->entry;

            if (entry.status() == Entry::DELETED)
            {
                lock.unlock();

                _callback(nullptr, std::nullopt);
            }
            else
            {
                auto optionalEntry = std::make_optional(entry);
                if constexpr (enableLRU)
                {
                    updateMRUAndCheck(*bucket, it);
                }

                lock.unlock();

                _callback(nullptr, std::move(optionalEntry));
            }
            return;
        }

        lock.unlock();

        auto prev = getPrev();
        if (prev)
        {
            prev->asyncGetRow(tableView, keyView,
                [this, prev, table = std::string(tableView), key = std::string(keyView), _callback](
                    Error::UniquePtr error, std::optional<Entry> entry) {
                    if (error)
                    {
                        _callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(StorageError::ReadError,
                                      "Get row from storage failed!", *error),
                            {});
                        return;
                    }

                    if (entry)
                    {
                        _callback(nullptr,
                            std::make_optional(importExistingEntry(table, key, std::move(*entry))));
                    }
                    else
                    {
                        _callback(nullptr, std::nullopt);
                    }
                });
        }
        else
        {
            _callback(nullptr, std::nullopt);
        }
    }

    task::Task<void> impl_setRows(concepts::bytebuffer::ByteBuffer auto const& tableName,
        concepts::storage::Keys auto const& keys, concepts::storage::Entries auto const& entries)
    {}

    task::Task<void> impl_createTable(concepts::bytebuffer::ByteBuffer auto const& tableName) {}

private:
    constexpr static int64_t DEFAULT_CAPACITY = 32L * 1024 * 1024;
    int64_t m_maxCapacity = DEFAULT_CAPACITY;

    struct Data
    {
        std::string table;
        std::string key;
        Entry entry;

        std::tuple<std::string_view, std::string_view> view() const
        {
            return std::make_tuple(std::string_view(table), std::string_view(key));
        }
    };

    using HashContainer = boost::multi_index_container<Data,
        boost::multi_index::indexed_by<boost::multi_index::ordered_unique<boost::multi_index::
                const_mem_fun<Data, std::tuple<std::string_view, std::string_view>, &Data::view>>>>;
    using LRUHashContainer = boost::multi_index_container<Data,
        boost::multi_index::indexed_by<
            boost::multi_index::ordered_unique<boost::multi_index::const_mem_fun<Data,
                std::tuple<std::string_view, std::string_view>, &Data::view>>,
            boost::multi_index::sequenced<>>>;
    using Container = std::conditional_t<withMRU, LRUHashContainer, HashContainer>;

    struct Bucket
    {
        Container container;
        std::mutex mutex;
        ssize_t capacity = 0;
    };
    uint32_t m_blockVersion = 0;
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

    void updateMRUAndCheck(
        Bucket& bucket, typename Container::template nth_index<0>::type::iterator it)
    {
        auto seqIt = bucket.container.template get<1>().iterator_to(*it);
        bucket.container.template get<1>().relocate(
            bucket.container.template get<1>().end(), seqIt);

        size_t clearCount = 0;
        while (bucket.capacity > m_maxCapacity && !bucket.container.empty())
        {
            auto& item = bucket.container.template get<1>().front();
            bucket.capacity -= item.entry.size();

            bucket.container.template get<1>().pop_front();
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