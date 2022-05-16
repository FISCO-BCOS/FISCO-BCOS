/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief KeyPage implementation of StorageInterface
 * @file KeyPageStorage.h
 * @author: xingqiangbai
 * @date: 2022-04-19
 */
#pragma once

#include "bcos-table/src/StateStorageInterface.h"
#include <boost/archive/basic_archive.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/core/ignore_unused.hpp>
#include <boost/format.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/key.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/property_map/property_map.hpp>
#include <cstddef>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string_view>
#include <thread>
#include <utility>

#define KeyPage_LOG(LEVEL) BCOS_LOG(LEVEL) << "[STORAGE][KeyPage]"
namespace std
{

template <>
struct hash<std::pair<std::string_view, std::string_view>>
{
    size_t operator()(const std::pair<std::string_view, std::string_view>& p) const
    {
        // calculate the hash result
        auto hash_result = std::hash<std::string_view>{}(p.first);
        boost::hash_combine(hash_result, std::hash<std::string_view>{}(p.second));
        return hash_result;
    }
};

template <>
struct hash<std::pair<std::string, std::string>>
{
    size_t operator()(const std::pair<std::string, std::string>& p) const
    {
        // calculate the hash result
        auto hash_result = std::hash<std::string>{}(p.first);
        boost::hash_combine(hash_result, std::hash<std::string>{}(p.second));
        return hash_result;
    }
};

}  // namespace std

namespace bcos::storage
{
constexpr static int32_t ARCHIVE_FLAG =
    boost::archive::no_header | boost::archive::no_codecvt | boost::archive::no_tracking;

class KeyPageStorage : public virtual storage::StateStorageInterface
{
public:
    using Ptr = std::shared_ptr<KeyPageStorage>;

    explicit KeyPageStorage(std::shared_ptr<StorageInterface> _prev, bool _returnPage = false,
        size_t _pageSize = 8 * 1024)
      : storage::StateStorageInterface(_prev),
        m_returnPage(_returnPage),
        m_pageSize(_pageSize),
        m_splitSize(_pageSize / 2),
        m_mergeSize(_pageSize / 4),
        m_buckets(std::thread::hardware_concurrency())
    {}

    KeyPageStorage(const KeyPageStorage&) = delete;
    KeyPageStorage& operator=(const KeyPageStorage&) = delete;

    KeyPageStorage(KeyPageStorage&&) = delete;
    KeyPageStorage& operator=(KeyPageStorage&&) = delete;

    virtual ~KeyPageStorage() { m_recoder.clear(); }

    void asyncGetPrimaryKeys(std::string_view table,
        const std::optional<storage::Condition const>& _condition,
        std::function<void(Error::UniquePtr, std::vector<std::string>)> _callback) override;

    void asyncGetRow(std::string_view tableView, std::string_view keyView,
        std::function<void(Error::UniquePtr, std::optional<Entry>)> _callback) override;

    void asyncGetRows(std::string_view tableView,
        const std::variant<const gsl::span<std::string_view const>,
            const gsl::span<std::string const>>& _keys,
        std::function<void(Error::UniquePtr, std::vector<std::optional<Entry>>)> _callback)
        override;

    void asyncSetRow(std::string_view tableView, std::string_view keyView, Entry entry,
        std::function<void(Error::UniquePtr)> callback) override;

    void parallelTraverse(bool onlyDirty, std::function<bool(const std::string_view& table,
                                              const std::string_view& key, const Entry& entry)>
                                              callback) const override;

    crypto::HashType hash(const bcos::crypto::Hash::Ptr& hashImpl) const override;

    void rollback(const Recoder& recoder) override;

    void setReturnPage(bool _returnPage) { m_returnPage = _returnPage; }

private:
    std::shared_ptr<StorageInterface> getPrev()
    {
        std::shared_lock<std::shared_mutex> lock(m_prevMutex);
        return m_prev;
    }


    struct PageInfo
    {
        // startKey is not involved in comparison
        std::string startKey;
        std::string endKey;
        uint16_t count = 0;
        uint16_t size = 0;
        bool operator<(const PageInfo& rhs) const { return endKey < rhs.endKey; }

    private:
        friend class boost::serialization::access;
        template <class Archive>
        void serialize(Archive& ar, const unsigned int version)
        {
            std::ignore = version;
            ar& startKey;
            ar& endKey;
            ar& count;
            ar& size;
        }
    };
    struct TableMeta
    {
        TableMeta() {}
        ~TableMeta() = default;
        TableMeta(const std::string_view value)
        {
            if (value.empty())
            {
                return;
            }
            boost::iostreams::stream<boost::iostreams::array_source> inputStream(
                value.data(), value.size());
            boost::archive::binary_iarchive archive(inputStream, ARCHIVE_FLAG);
            archive >> *this;
        }
        TableMeta(const TableMeta& t) { pages = t.pages; }
        TableMeta& operator=(const TableMeta& t)
        {
            pages = t.pages;
            return *this;
        }
        TableMeta(TableMeta&& t) { pages = std::move(t.pages); }
        TableMeta& operator=(TableMeta&& t)
        {
            if (this != &t)
            {
                pages = std::move(t.pages);
            }
            return *this;
        }

        auto lower_bound(std::string_view key)
        {
            return std::lower_bound(pages.begin(), pages.end(), key,
                [](const PageInfo& lhs, const std::string_view& rhs) {
                    return std::string_view(lhs.endKey) < rhs;
                });
        }
        auto upper_bound(std::string_view key)
        {
            return std::upper_bound(pages.begin(), pages.end(), key,
                [](const std::string_view& rhs, const PageInfo& lhs) {
                    return rhs < std::string_view(lhs.endKey);
                });
        }
        std::optional<std::string> getPageKeyNoLock(std::string_view key)
        {
            if (pages.empty())
            {  // if pages is empty
                return std::nullopt;
            }
            auto it = lower_bound(key);
            if (it != pages.end())
            {
                return it->startKey;
            }
            return pages.rbegin()->startKey;
        }

        std::optional<std::string> getNextPageKeyNoLock(std::string_view key)
        {
            if (key.empty())
            {
                return std::nullopt;
            }
            auto it = upper_bound(key);
            if (it != pages.end())
            {
                return it->startKey;
            }
            return std::nullopt;
        }

        std::pair<std::list<PageInfo>&, std::shared_lock<std::shared_mutex>> getAllPageInfo()
        {
            return std::make_pair(std::ref(pages), std::shared_lock(mutex));
        }
        void insertPageInfo(PageInfo&& pageInfo)
        {
            std::unique_lock lock(mutex);
            insertPageInfoNoLock(std::move(pageInfo));
        }
        void insertPageInfoNoLock(PageInfo&& pageInfo)
        {
            assert(!pageInfo.startKey.empty());
            assert(!pageInfo.endKey.empty());
            if (pageInfo.startKey.empty() || pageInfo.endKey.empty())
            {
                KeyPage_LOG(FATAL) << LOG_DESC("insert empty pageInfo");
                return;
            }
            auto it = lower_bound(pageInfo.endKey);
            auto newIt = pages.insert(it, std::move(pageInfo));
            if (c_fileLogLevel >= TRACE)
            {
                KeyPage_LOG(TRACE)
                    << LOG_DESC("insert new pageInfo") << LOG_KV("startKey", toHex(newIt->startKey))
                    << LOG_KV("endKey", toHex(newIt->endKey)) << LOG_KV("count", newIt->count)
                    << LOG_KV("size", newIt->size);
            }
        }
        std::pair<std::optional<std::string>, std::unique_lock<std::shared_mutex>> updatePageInfo(
            std::string_view oldEndKey, const std::string& startKey, const std::string& endKey,
            size_t count, size_t size)
        {
            std::unique_lock lock(mutex);
            auto oldStartKey = updatePageInfoNoLock(oldEndKey, startKey, endKey, count, size);
            return std::make_pair(oldStartKey, std::move(lock));
        }

        std::optional<std::string> updatePageInfoNoLock(std::string_view oldEndKey,
            const std::string& startKey, const std::string& endKey, size_t count, size_t size)
        {
            std::optional<std::string> oldStartKey;
            auto it = lower_bound(oldEndKey);
            if (it != pages.end())
            {
                if (count > 0)
                {  // count == 0, means the page is empty, the rage is empty
                    if (it->startKey != startKey)
                    {
                        if (c_fileLogLevel >= TRACE)
                        {
                            KeyPage_LOG(TRACE)
                                << LOG_DESC("updatePageInfo")
                                << LOG_KV("startKey", toHex(it->startKey))
                                << LOG_KV("newStartKey", toHex(startKey))
                                << LOG_KV("count", it->count) << LOG_KV("newCount", count)
                                << LOG_KV("size", it->size) << LOG_KV("newSize", size);
                        }
                        oldStartKey = it->startKey;
                        it->startKey = startKey;
                    }
                    if (it->endKey != endKey)
                    {
                        if (c_fileLogLevel >= TRACE)
                        {
                            KeyPage_LOG(TRACE)
                                << LOG_DESC("updatePageInfo") << LOG_KV("endKey", toHex(it->endKey))
                                << LOG_KV("newEndKey", toHex(endKey)) << LOG_KV("count", it->count)
                                << LOG_KV("newCount", count) << LOG_KV("size", it->size)
                                << LOG_KV("newSize", size);
                        }
                        it->endKey = endKey;
                    }
                }
                it->count = count;
                it->size = size;
            }
            return oldStartKey;
        }

        void deletePageInfoNoLock(std::string_view endkey)
        {  // remove current page info and update next page start key
            decltype(pages)::iterator it = lower_bound(endkey);
            if (it != pages.end() && it->endKey == endkey)
            {
                pages.erase(it);
            }
        }
        size_t size()
        {
            std::shared_lock lock(mutex);
            return pages.size();
        }

        std::unique_lock<std::shared_mutex> lock() { return std::unique_lock(mutex); }
        std::shared_lock<std::shared_mutex> rLock() { return std::shared_lock(mutex); }

        friend std::ostream& operator<<(
            std::ostream& os, const bcos::storage::KeyPageStorage::TableMeta& meta)
        {
            auto lock = std::shared_lock(meta.mutex);
            os << "[";
            for (auto& pageInfo : meta.pages)
            {
                os << "{"
                   << "startKey=" << toHex(pageInfo.startKey)
                   << ",endKey=" << toHex(pageInfo.endKey) << ",count=" << pageInfo.count
                   << ",size=" << pageInfo.size << "}";
            }
            os << "]";
            return os;
        }

    private:
        mutable std::shared_mutex mutex;
        std::list<PageInfo> pages;
        friend class boost::serialization::access;

        template <class Archive>
        void save(Archive& ar, const unsigned int version) const
        {
            std::ignore = version;
            ar&(uint32_t)pages.size();
            for (auto& i : pages)
            {
                ar& i;
            }
        }
        template <class Archive>
        void load(Archive& ar, const unsigned int version)
        {
            std::ignore = version;
            uint32_t size = 0;
            ar& size;
            for (size_t i = 0; i < size; ++i)
            {
                PageInfo info;
                ar& info;
                pages.push_back(std::move(info));
            }
        }
        BOOST_SERIALIZATION_SPLIT_MEMBER()
    };

    struct Page
    {
        Page() : m_size(0) {}
        ~Page() = default;
        Page(const std::string_view value)
        {
            if (value.empty())
            {
                return;
            }
            boost::iostreams::stream<boost::iostreams::array_source> inputStream(
                value.data(), value.size());
            boost::archive::binary_iarchive archive(inputStream, ARCHIVE_FLAG);
            archive >> *this;
        }
        Page(const Page& p) = delete;
        Page& operator=(const Page& p) = delete;
        Page(Page&& p)
        {
            entries = std::move(p.entries);
            m_size = p.m_size;
            m_count = p.m_count;
        }
        Page& operator=(Page&& p)
        {
            if (this != &p)
            {
                entries = std::move(p.entries);
                m_size = p.m_size;
                m_count = p.m_count;
            }
            return *this;
        }

        std::optional<Entry> getEntry(std::string_view key)
        {
            std::shared_lock lock(mutex);
            auto it = entries.find(key);
            if (it != entries.end() && it->second.status() != Entry::Status::DELETED &&
                it->second.status() != Entry::EMPTY)
            {
                return std::make_optional(it->second);
            }
            return std::nullopt;
        }
        std::pair<std::map<std::string, Entry, std::less<>>&, std::unique_lock<std::shared_mutex>>
        getEntries()
        {
            std::unique_lock lock(mutex);
            return std::make_pair(std::ref(entries), std::move(lock));
        }
        std::tuple<std::optional<Entry>, bool> setEntry(std::string_view key, Entry entry)
        {  // TODO: do not exist entry optimization: insert a none status entry to cache, then
            // entry status none should return null optional
            bool pageInfoChanged = false;
            std::optional<Entry> ret;
            std::unique_lock lock(mutex);
            auto it = entries.find(key);
            if (it != entries.end())
            {  // delete exist entry
                m_size -= it->second.size();
                if (entry.status() != Entry::Status::DELETED)
                {
                    m_size += entry.size();
                    if (it->second.status() == Entry::Status::DELETED)
                    {
                        ++m_count;
                    }
                }
                else
                {
                    entry.set(std::string());
                    entry.setStatus(Entry::Status::DELETED);
                    if (it->second.status() != Entry::Status::DELETED)
                    {
                        --m_count;
                    }
                }
                ret = std::move(it->second);
                it->second = std::move(entry);
                // KeyPage_LOG(TRACE) << LOG_DESC("setEntry update") << LOG_KV("key", key)
                //                    << LOG_KV("status", (int)it->second.status());
            }
            else
            {
                if (entry.status() != Entry::Status::DELETED)
                {
                    ++m_count;
                    m_size += entry.size();
                }
                if (entries.size() == 0 || key < entries.begin()->first ||
                    key > entries.rbegin()->first)
                {
                    pageInfoChanged = true;
                }
                // KeyPage_LOG(TRACE) << LOG_DESC("setEntry insert") << LOG_KV("key", key)
                //                    << LOG_KV("pageInfoChanged", pageInfoChanged)
                //                    << LOG_KV("status", (int)entry.status());
                entries.insert(std::make_pair(std::string(key), std::move(entry)));
            }
            return std::make_tuple(ret, pageInfoChanged);
        }
        size_t size() const
        {
            std::shared_lock lock(mutex);
            return m_size;
        }
        size_t count() const
        {
            std::shared_lock lock(mutex);
            return m_count;
        }
        std::string startKey()
        {
            std::shared_lock lock(mutex);
            if (entries.empty())
            {
                return "";
            }
            return entries.begin()->first;
        }
        std::string endKey()
        {
            std::shared_lock lock(mutex);
            if (entries.empty())
            {
                return "";
            }
            return entries.rbegin()->first;
        }
        Page split(size_t threshold)
        {
            size_t mySize = 0;
            Page page;
            std::unique_lock lock(mutex);
            // split this page to two pages
            for (auto iter = entries.begin(); iter != entries.end(); ++iter)
            {
                auto next = iter;
                if (mySize >= threshold)
                {
                    --next;
                    page.m_size += iter->second.size();
                    if (iter->second.status() != Entry::Status::DELETED)
                    {
                        --m_count;
                        ++page.m_count;
                    }
                    auto ret = page.entries.insert(entries.extract(iter));
                    assert(ret.inserted);
                }
                else
                {
                    mySize += iter->second.size();
                }
                iter = next;
            }
            m_size = mySize;
            if (page.size() == 0)
            {  // the last value is big value
                auto last = --entries.end();
                page.m_size += last->second.size();
                m_size -= last->second.size();
                KeyPage_LOG(DEBUG) << "split large value" << LOG_KV("key", toHex(last->first))
                                   << LOG_KV("size", last->second.size());
                if (last->second.status() != Entry::Status::DELETED)
                {
                    --m_count;
                    ++page.m_count;
                }
                auto ret = page.entries.insert(entries.extract(last));
                assert(ret.inserted);
            }
            return page;
        }

        void merge(Page&& p)
        {
            if (this != &p)
            {
                std::unique_lock lock(mutex);
                for (auto iter = p.entries.begin(); iter != p.entries.end();)
                {
                    m_size += iter->second.size();
                    p.m_size -= iter->second.size();
                    if (iter->second.status() != Entry::Status::DELETED)
                    {
                        ++m_count;
                        --p.m_count;
                    }
                    entries.insert(p.entries.extract(iter));
                    iter = p.entries.begin();
                }
            }
        }
        crypto::HashType hash(
            const std::string& table, const bcos::crypto::Hash::Ptr& hashImpl) const
        {
            bcos::crypto::HashType pageHash;
            std::shared_lock lock(mutex);
            auto hash = hashImpl->hash(table);
            for (auto iter = entries.cbegin(); iter != entries.cend(); ++iter)
            {
                if (!iter->second.dirty())
                {
                    continue;
                }
                auto entryHash = hash ^ hashImpl->hash(iter->first) ^
                                 iter->second.hash(table, iter->first, hashImpl);
                pageHash ^= entryHash;
            }
            return pageHash;
        }

        void rollback(const Recoder::Change& change)
        {
            std::unique_lock lock(mutex);
            auto it = entries.find(change.key);
            if (change.entry)
            {
                if (it != entries.end())
                {  // update
                    if (c_fileLogLevel >= bcos::LogLevel::TRACE)
                    {
                        KeyPage_LOG(TRACE)
                            << "Revert update: " << change.table << " | " << toHex(change.key)
                            << " | " << toHex(change.entry->get());
                    }
                    m_size -= it->second.size();
                    auto& rollbackEntry = change.entry;
                    it->second = std::move(rollbackEntry.value());
                    m_size += it->second.size();
                }
                else
                {  // delete
                    if (c_fileLogLevel >= bcos::LogLevel::TRACE)
                    {
                        KeyPage_LOG(TRACE)
                            << "Revert delete: " << change.table << " | " << toHex(change.key)
                            << " | " << toHex(change.entry->get());
                    }
                    entries[change.key] = std::move(change.entry.value());
                }
            }
            else
            {  // rollback insert
                if (it != entries.end())
                {  // insert or update
                    if (c_fileLogLevel >= bcos::LogLevel::TRACE)
                    {
                        KeyPage_LOG(TRACE)
                            << "Revert insert: " << change.table << " | " << toHex(change.key);
                    }
                    m_size -= it->second.size();
                    entries.erase(it);
                }
                else
                {  // delete a key not exist
                    KeyPage_LOG(DEBUG)
                        << "Revert invalid delete: " << change.table << " | " << toHex(change.key);
                }
            }
        }
        std::unique_lock<std::shared_mutex> lock() { return std::unique_lock(mutex); }
        std::shared_lock<std::shared_mutex> rLock() { return std::shared_lock(mutex); }

    private:
        //   PageInfo* pageInfo;
        mutable std::shared_mutex mutex;
        std::map<std::string, Entry, std::less<>> entries;
        uint32_t m_size = 0;
        uint32_t m_count = 0;
        friend class boost::serialization::access;

        template <class Archive>
        void save(Archive& ar, const unsigned int version) const
        {
            std::ignore = version;
            ar&(uint32_t)m_count;
            for (auto& i : entries)
            {
                if (i.second.status() == Entry::Status::DELETED)
                {
                    continue;
                }
                ar& i.first;
                auto value = i.second.get();
                ar&(uint32_t)value.size();
                for (auto c : value)
                {
                    ar& c;
                }
            }
        }
        template <class Archive>
        void load(Archive& ar, const unsigned int version)
        {
            std::ignore = version;
            uint32_t count = 0;
            ar& count;
            m_count = count;
            for (size_t i = 0; i < m_count; ++i)
            {
                std::string key;
                ar& key;
                uint32_t len = 0;
                ar& len;
                m_size += len;
                std::string value(len, '\0');
                for (size_t j = 0; j < len; ++j)
                {
                    ar& value[j];
                }
                Entry e;
                e.set(std::move(value));
                // e.setDirty(false);
                e.setStatus(Entry::Status::NORMAL);
                entries.emplace(std::move(key), std::move(e));
            }
        }
        BOOST_SERIALIZATION_SPLIT_MEMBER()
    };

    struct Data
    {
        enum Type : int8_t
        {
            Page = 0,
            TableMeta = 1,
            NormalEntry = 2,
        };
        Data(){};
        ~Data() = default;
        Data(std::string _table, std::string _key, Entry _entry)
          : table(std::move(_table)),
            key(std::move(_key)),
            type(Type::NormalEntry),
            entry(std::move(_entry)){};
        Data(const Data& d) = delete;
        Data& operator=(const Data& d) = delete;
        Data(Data&& d) = default;
        Data& operator=(Data&& d) = default;
        std::string table;
        std::string key;
        Type type = Type::NormalEntry;
        Entry entry;
        std::variant<KeyPageStorage::Page, KeyPageStorage::TableMeta> data;
        std::pair<std::string_view, std::string_view> view() const
        {
            return std::make_pair(std::string_view(table), std::string_view(key));
        }
    };

    struct Bucket
    {
        Bucket() {}
        ~Bucket() = default;
        std::unordered_map<std::pair<std::string, std::string>, Data> container;
        std::shared_mutex mutex;
        std::optional<Data*> find(std::string_view table, std::string_view key)
        {
            std::shared_lock lock(mutex);
            auto it = container.find(std::make_pair(std::string(table), std::string(key)));
            if (it != container.end())
            {
                return &it->second;
            }
            return std::nullopt;
        }
        std::unique_lock<std::shared_mutex> lock() { return std::unique_lock(mutex); }
        std::shared_lock<std::shared_mutex> rLock() { return std::shared_lock(mutex); }
    };

    size_t getBucketIndex(std::string_view table, std::string_view key) const
    {
        auto hash = std::hash<std::string_view>{}(table);
        std::ignore = key;
        // boost::hash_combine(hash, std::hash<std::string_view>{}(key));
        return hash % m_buckets.size();
    }

    Data* updatePageStartKey(std::string table, std::string oldStartKey, std::string newStartKey)
    {
        auto [bucket, lock] = getMutBucket(table, oldStartKey);
        boost::ignore_unused(lock);
        auto n = bucket->container.extract(std::make_pair(table, oldStartKey));
        n.key().second = newStartKey;
        auto it = bucket->container.find(std::make_pair(table, newStartKey));
        if (it != bucket->container.end())
        {  // erase old page to update data
            bucket->container.erase(it);
        }
        auto ret = bucket->container.insert(std::move(n));
        assert(ret.inserted);
        return &ret.position->second;
        // the bucket also need to be updated
    }

    std::pair<Bucket*, std::shared_lock<std::shared_mutex>> getBucket(
        std::string_view table, std::string_view key)
    {
        auto index = getBucketIndex(table, key);
        auto& bucket = m_buckets[index];
        return std::make_pair(&bucket, std::shared_lock<std::shared_mutex>(bucket.mutex));
    }

    std::pair<Bucket*, std::unique_lock<std::shared_mutex>> getMutBucket(
        std::string_view table, std::string_view key)
    {
        auto index = getBucketIndex(table, key);
        auto& bucket = m_buckets[index];
        return std::make_pair(&bucket, std::unique_lock<std::shared_mutex>(bucket.mutex));
    }

    // if data not exist, create an empty one
    std::tuple<Error::UniquePtr, std::optional<Data*>> getData(
        std::string_view tableView, std::string_view key)
    {
        // find from cache
        auto [bucket, lock] = getBucket(tableView, key);
        boost::ignore_unused(lock);
        auto keyPair = std::make_pair(std::string(tableView), std::string(key));
        decltype(bucket->container)::iterator it = bucket->container.find(keyPair);
        if (it != bucket->container.end())
        {
            auto data = &it->second;
            lock.unlock();
            return std::make_tuple(std::unique_ptr<Error>(nullptr), std::make_optional(data));
        }
        lock.unlock();
        // find from prev
        Data d;
        d.table = std::string(tableView);
        d.key = std::string(key);
        d.type = key.empty() ? Data::Type::TableMeta : Data::Type::Page;
        auto [error, entry] = getRawEntryFromStorage(tableView, key);
        if (error)
        {
            return std::make_tuple(std::move(error), std::nullopt);
        }
        if (entry)
        {
            if (key.empty())
            {
                auto meta = KeyPageStorage::TableMeta(entry.value().get());
                if (c_fileLogLevel >= TRACE)
                {
                    KeyPage_LOG(TRACE)
                        << LOG_DESC("import TableMeta") << LOG_KV("table", tableView)
                        << LOG_KV("len", entry.value().size()) << LOG_KV("meta", meta);
                }
                d.data = std::move(meta);
            }
            else
            {
                auto page = KeyPageStorage::Page(entry.value().get());
                if (c_fileLogLevel >= TRACE)
                {
                    KeyPage_LOG(TRACE)
                        << LOG_DESC("import Page") << LOG_KV("table", tableView)
                        << LOG_KV("startKey", toHex(page.startKey()))
                        << LOG_KV("endKey", toHex(page.endKey()))
                        << LOG_KV("len", entry.value().size()) << LOG_KV("count", page.count())
                        << LOG_KV("pageSize", page.size());
                }
                d.data = std::move(page);
            }
            d.entry = std::move(entry.value());
            d.entry.setStatus(Entry::Status::NORMAL);
        }
        else
        {
            if (c_fileLogLevel >= TRACE)
            {
                KeyPage_LOG(TRACE) << LOG_DESC("create empty data") << LOG_KV("table", tableView)
                                   << LOG_KV("key", toHex(key));
            }
            if (key.empty())
            {
                d.data = KeyPageStorage::TableMeta();
            }
            else
            {
                d.data = KeyPageStorage::Page();
            }
            d.entry.setStatus(Entry::Status::EMPTY);
        }
        // d.entry.setDirty(false);
        // insert into cache
        {
            auto [bucket, writeLock] = getMutBucket(d.table, d.key);
            boost::ignore_unused(writeLock);
            auto& data =
                bucket->container.emplace(std::make_pair(keyPair, std::move(d))).first->second;

            return std::make_tuple(nullptr, std::make_optional(&data));
        }
    }

    std::pair<Error::UniquePtr, std::optional<Entry>> getRawEntry(
        std::string_view table, std::string_view key)
    {
        auto [bucket, lock] = getBucket(table, key);
        boost::ignore_unused(lock);
        auto it = bucket->container.find(std::make_pair(std::string(table), std::string(key)));
        if (it != bucket->container.end())
        {
            auto& entry = it->second.entry;
            lock.unlock();
            return std::make_pair(nullptr, std::make_optional(entry));
        }
        lock.unlock();
        // find from prev
        auto [error, entryOption] = getRawEntryFromStorage(table, key);
        if (!error && entryOption)
        {
            auto entry = importExistingEntry(table, key, std::move(entryOption.value()));
            return std::make_pair(nullptr, std::make_optional(std::move(entry)));
        }
        return std::make_pair(std::move(error), entryOption);
    }

    std::pair<Error::UniquePtr, std::optional<Entry>> getRawEntryFromStorage(
        std::string_view table, std::string_view key)
    {
        auto prev = getPrev();  // prev must not null
        if (!prev)
        {
            return std::make_pair(nullptr, std::nullopt);
        }
        std::promise<std::pair<Error::UniquePtr, std::optional<Entry>>> getPromise;
        prev->asyncGetRow(table, key, [&](Error::UniquePtr error, std::optional<Entry> entry) {
            getPromise.set_value({std::move(error), std::move(entry)});
        });
        return getPromise.get_future().get();
    }

    std::pair<Error::UniquePtr, std::optional<Entry>> getEntryFromPage(
        std::string_view table, std::string_view key)
    {
        auto [error, data] = getData(table, "");
        if (error)
        {
            return std::make_pair(std::move(error), std::nullopt);
        }
        auto& meta = std::get<1>(data.value()->data);
        auto readLock = meta.rLock();
        if (key.empty())
        {  // table meta
            if (c_fileLogLevel >= TRACE)
            {
                KeyPage_LOG(TRACE) << LOG_DESC("return meta entry") << LOG_KV("table", table)
                                   << LOG_KV("meta.size", meta.size())
                                   << LOG_KV("dirty", data.value()->entry.dirty());
            }
            if (meta.size() > 0)
            {
                Entry entry;
                entry.setObject(meta);
                // entry.setDirty(data.value()->entry.dirty());
                entry.setStatus(data.value()->entry.status());
                return std::make_pair(nullptr, std::move(entry));
            }
            return std::make_pair(nullptr, std::nullopt);
        }
        auto pageKey = meta.getPageKeyNoLock(key);
        if (pageKey)
        {
            auto [error, pageData] = getData(table, pageKey.value());
            if (error)
            {
                return std::make_pair(std::move(error), std::nullopt);
            }
            if (pageData.has_value())
            {
                if (pageData.value()->entry.status() == Entry::Status::EMPTY)
                {
                    return std::make_pair(nullptr, std::nullopt);
                }

                auto& page = std::get<0>(pageData.value()->data);
                if (pageKey.value() == key && m_returnPage)
                {
                    auto pageReadLock = page.rLock();
                    if (c_fileLogLevel >= TRACE)
                    {
                        KeyPage_LOG(TRACE) << LOG_DESC("return page entry")
                                           << LOG_KV("table", table) << LOG_KV("key", toHex(key))
                                           << LOG_KV("pageKey", toHex(pageKey.value()))
                                           << LOG_KV("page.size", page.size())
                                           << LOG_KV("dirty", data.value()->entry.dirty());
                    }
                    if (page.size() > 0)
                    {
                        Entry entry;
                        entry.setObject(page);
                        // entry.setDirty(pageData.value()->entry.dirty());
                        entry.setStatus(pageData.value()->entry.status());
                        return std::make_pair(nullptr, std::move(entry));
                    }
                    return std::make_pair(nullptr, std::nullopt);
                }
                auto entry = page.getEntry(key);
                // if (c_fileLogLevel >= TRACE)
                // {
                //     KeyPage_LOG(TRACE)
                //         << LOG_DESC("return entry") << LOG_KV("table", table)
                //         << LOG_KV("key", toHex(key)) << LOG_KV("pageKey", toHex(pageKey.value()))
                //         << LOG_KV("has_value", entry.has_value());
                // }
                return std::make_pair(nullptr, std::move(entry));
            }
            else
            {
                return std::make_pair(nullptr, std::nullopt);
            }
        }
        return std::make_pair(nullptr, std::nullopt);
    }

    void insertNewPage(std::string_view tableView, std::string_view keyView,
        KeyPageStorage::TableMeta& meta, Page&& page)
    {
        // prepare page info
        PageInfo info;
        info.startKey = page.startKey();
        info.endKey = page.endKey();
        info.count = page.count();
        info.size = page.size();

        // insert page
        Data d;
        d.table = std::string(tableView);
        d.key = std::string(keyView);
        d.type = Data::Type::Page;
        d.data = std::move(page);
        // d.entry.setDirty(true);
        d.entry.setStatus(Entry::Status::MODIFIED);
        auto [bucket, lock] = getMutBucket(tableView, keyView);
        boost::ignore_unused(lock);
        bucket->container.emplace(std::make_pair(std::make_pair(d.table, d.key), std::move(d)));
        lock.unlock();
        // update table meta
        meta.insertPageInfoNoLock(std::move(info));
    }

    Error::UniquePtr setEntryToPage(std::string table, std::string key, Entry entry)
    {
        auto [error, data] = getData(table, "");
        if (error)
        {
            return std::move(error);
        }
        auto& meta = std::get<1>(data.value()->data);
        auto metaWriteLock = meta.lock();
        // insert or update
        auto pageKeyOption = meta.getPageKeyNoLock(key);
        std::string_view pageKey = key;
        if (pageKeyOption)
        {
            pageKey = pageKeyOption.value();
        }
        std::optional<Entry> entryOld;

        auto [e, pageDataOp] = getData(table, pageKey);
        auto pageData = pageDataOp.value();
        if (e)
        {
            return std::move(e);
        }
        // if new entry is too big, it will trigger split
        auto page = &std::get<0>(pageData->data);
        {
            auto ret = page->setEntry(key, std::move(entry));
            entryOld = std::move(std::get<0>(ret));
            auto pageInfoChanged = std::move(std::get<1>(ret));
            if (pageInfoChanged)
            {
                if (pageData->entry.status() == Entry::Status::EMPTY)
                {  // new page insert, if entries is empty means page delete entry which not exist
                    meta.insertPageInfoNoLock(PageInfo{page->startKey(), page->endKey(),
                        (uint16_t)page->count(), (uint16_t)page->size()});
                    // pageData->entry.setStatus(Entry::Status::NORMAL);
                    pageData->entry.setStatus(Entry::Status::MODIFIED);
                }
                else
                {
                    auto oldStartKey = meta.updatePageInfoNoLock(
                        pageKey, page->startKey(), page->endKey(), page->count(), page->size());
                    if (oldStartKey)
                    {  // the page key is changed, 1. delete the first key, 2. insert a smaller key
                        // if the startKey of page changed, the container also need to be updated
                        if (page->count() > 0)
                        {
                            pageData->key = page->startKey();
                            pageData =
                                updatePageStartKey(table, oldStartKey.value(), page->startKey());
                            page = &std::get<0>(pageData->data);
                            // pageData->entry.setStatus(Entry::Status::NORMAL);
                            pageData->entry.setStatus(Entry::Status::MODIFIED);
                        }
                        else
                        {  // if page is empty because delete, not update startKey and mark as
                           // deleted
                            pageData->entry.setStatus(Entry::Status::DELETED);
                        }
                    }
                }
            }
            // page is modified, the meta maybe modified, mark meta as dirty
            // data.value()->entry.setDirty(true);
            // data.value()->entry.setStatus(Entry::Status::NORMAL);
            data.value()->entry.setStatus(Entry::Status::MODIFIED);
        }
        if (page->size() > m_pageSize && page->count() > 1)
        {  // split page
            KeyPage_LOG(DEBUG) << LOG_DESC("trigger split page") << LOG_KV("pageSize", page->size())
                               << LOG_KV("pageCount", page->count());
            auto newPage = page->split(m_splitSize);
            // update old meta pageInfo
            auto oldStartKey = meta.updatePageInfoNoLock(
                pageKey, page->startKey(), page->endKey(), page->count(), page->size());
            if (oldStartKey)
            {  // if the startKey of page changed, the container also need to be updated
                updatePageStartKey(table, oldStartKey.value(), page->startKey());
            }
            KeyPage_LOG(DEBUG) << LOG_DESC("split page finished") << LOG_KV("table", table)
                               << LOG_KV("pageStart", toHex(page->startKey()))
                               << LOG_KV("pageEnd", toHex(page->endKey()))
                               << LOG_KV("newPageStart", toHex(newPage.startKey()))
                               << LOG_KV("newPageEnd", toHex(newPage.endKey()))
                               << LOG_KV("pageCount", page->count())
                               << LOG_KV("newPageCount", newPage.count())
                               << LOG_KV("pageSize", page->size())
                               << LOG_KV("newPageSize", newPage.size());
            // insert new page to container, newPageInfo to meta
            insertNewPage(table, newPage.startKey(), meta, std::move(newPage));
        }
        else if (page->size() < m_mergeSize)
        {  // merge operation
            // get next page, check size and merge current into next
            auto nextPageKey = meta.getNextPageKeyNoLock(page->endKey());
            if (nextPageKey)
            {
                auto [error, nextPageData] = getData(table, nextPageKey.value());
                boost::ignore_unused(error);
                assert(!error);
                auto& nextPage = std::get<0>(nextPageData.value()->data);
                if (nextPage.size() < m_splitSize)
                {
                    auto endKey = page->endKey();
                    auto nexEndKey = nextPage.endKey();
                    KeyPage_LOG(DEBUG)
                        << LOG_DESC("merge page") << LOG_KV("table", table) << LOG_KV("key", key)
                        << LOG_KV("pageKey", pageKey) << LOG_KV("pageStart", page->startKey())
                        << LOG_KV("pageEnd", endKey) << LOG_KV("pageCount", page->count())
                        << LOG_KV("pageSize", page->size())
                        << LOG_KV("nextPageKey", nextPageKey.value())
                        << LOG_KV("nextPageStart", nextPage.startKey())
                        << LOG_KV("nextPageEnd", nexEndKey)
                        << LOG_KV("nextPageCount", nextPage.count())
                        << LOG_KV("nextPageSize", nextPage.size());
                    nextPage.merge(std::move(*page));
                    // remove current page info and update next page info
                    meta.deletePageInfoNoLock(endKey);
                    auto oldStartKey = meta.updatePageInfoNoLock(nexEndKey, nextPage.startKey(),
                        nextPage.endKey(), nextPage.count(), nextPage.size());
                    if (oldStartKey)
                    {  // if the startKey of page changed, the container also need to be updated
                        updatePageStartKey(table, oldStartKey.value(), nextPage.startKey());
                    }
                    // old page also need write to disk to clean data, so not remove old page
                    // nextPageData.value()->entry.setDirty(true);
                    // pageData->entry.setDirty(true);
                    nextPageData.value()->entry.setStatus(Entry::Status::MODIFIED);
                    pageData->entry.setStatus(Entry::Status::MODIFIED);
                }
            }
        }
        if (m_recoder.local())
        {
            m_recoder.local()->log(
                Recoder::Change(std::move(table), std::move(key), std::move(entryOld)));
        }
        return nullptr;
    }

    Entry importExistingEntry(std::string_view table, std::string_view key, Entry entry)
    {
        if (m_readOnly)
        {
            return entry;
        }

        // entry.setDirty(false);
        entry.setStatus(Entry::NORMAL);
        KeyPage_LOG(DEBUG) << "import entry, " << table << " | " << key;
        auto [bucket, lock] = getMutBucket(table, key);
        boost::ignore_unused(lock);
        auto it = bucket->container.find(std::make_pair(std::string(table), std::string(key)));
        if (it == bucket->container.end())
        {
            Data d{std::string(table), std::string(key), std::move(entry)};
            it = bucket->container
                     .emplace(std::make_pair(std::make_pair(d.table, d.key), std::move(d)))
                     .first;
        }
        else
        {
            KeyPage_LOG(WARNING) << "Fail import existsing entry, " << table << " | " << toHex(key);
        }

        return it->second.entry;
    }
    bool m_returnPage = false;
    size_t m_pageSize = 8 * 1024;
    size_t m_splitSize;
    size_t m_mergeSize;
    std::vector<Bucket> m_buckets;
};

}  // namespace bcos::storage
