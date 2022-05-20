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
#include <exception>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <sstream>
#include <string_view>
#include <thread>
#include <utility>

#define KeyPage_LOG(LEVEL) BCOS_LOG(LEVEL) << "[STORAGE-KeyPage]"
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

const char* const TABLE_META_KEY = "";
class KeyPageStorage : public virtual storage::StateStorageInterface
{
public:
    using Ptr = std::shared_ptr<KeyPageStorage>;

    explicit KeyPageStorage(std::shared_ptr<StorageInterface> _prev, size_t _pageSize = 8 * 1024)
      : storage::StateStorageInterface(_prev),
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
            return pages.lower_bound(PageInfo{"", std::string(key)});
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
            auto it = pages.upper_bound(PageInfo{"", std::string(key)});
            if (it != pages.end())
            {
                return it->startKey;
            }
            return std::nullopt;
        }

        std::set<PageInfo>& getAllPageInfoNoLock() { return std::ref(pages); }
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
                    << LOG_KV("endKey", toHex(newIt->endKey)) << LOG_KV("valid", newIt->count)
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
                auto node = pages.extract(it);
                if (c_fileLogLevel >= TRACE)
                {
                    KeyPage_LOG(TRACE)
                        << LOG_DESC("updatePageInfo") << LOG_KV("oldEndKey", toHex(oldEndKey))
                        << LOG_KV("startKey", toHex(node.value().startKey))
                        << LOG_KV("newStartKey", toHex(startKey))
                        << LOG_KV("endKey", toHex(node.value().endKey))
                        << LOG_KV("newEndKey", toHex(endKey)) << LOG_KV("valid", node.value().count)
                        << LOG_KV("newValid", count) << LOG_KV("size", node.value().size)
                        << LOG_KV("newSize", size);
                }
                // assert(count > 0);
                // if (count > 0)
                {  // count == 0, means the page is empty, the rage is empty
                    if (node.value().startKey != startKey)
                    {
                        oldStartKey = node.value().startKey;
                        node.value().startKey = startKey;
                    }
                    if (node.value().endKey != endKey)
                    {
                        node.value().endKey = endKey;
                    }
                }
                if (count == 0)
                {
                    KeyPage_LOG(ERROR)
                        << LOG_DESC("updatePageInfo empty") << LOG_KV("startKey", toHex(startKey))
                        << LOG_KV("oldStartKey", toHex(oldEndKey))
                        << LOG_KV("endKey", toHex(endKey)) << LOG_KV("valid", count)
                        << LOG_KV("size", size);
                }
                node.value().count = count;
                node.value().size = size;
                pages.insert(std::move(node));
            }
            else
            {
                KeyPage_LOG(ERROR)
                    << LOG_DESC("updatePageInfo not found") << LOG_KV("startKey", toHex(startKey))
                    << LOG_KV("oldStartKey", toHex(oldEndKey)) << LOG_KV("endKey", toHex(endKey))
                    << LOG_KV("valid", count) << LOG_KV("size", size);
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

        std::unique_lock<std::shared_mutex> lock() const { return std::unique_lock(mutex); }
        std::shared_lock<std::shared_mutex> rLock() const { return std::shared_lock(mutex); }

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
        std::set<PageInfo> pages;
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
                pages.insert(std::move(info));
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
        Page(const Page& p)
        {
            entries = p.entries;
            m_size = p.m_size;
            m_validCount = p.m_validCount;
            m_invalidStartKeys = p.m_invalidStartKeys;
        }
        Page& operator=(const Page& p)
        {
            if (this != &p)
            {
                entries = p.entries;
                m_size = p.m_size;
                m_validCount = p.m_validCount;
                m_invalidStartKeys = p.m_invalidStartKeys;
            }
            return *this;
        }
        Page(Page&& p)
        {
            entries = std::move(p.entries);
            m_size = p.m_size;
            m_validCount = p.m_validCount;
            m_invalidStartKeys = std::move(p.m_invalidStartKeys);
        }
        Page& operator=(Page&& p)
        {
            if (this != &p)
            {
                entries = std::move(p.entries);
                m_size = p.m_size;
                m_validCount = p.m_validCount;
            }
            return *this;
        }

        std::optional<Entry> getEntry(std::string_view key)
        {
            std::shared_lock lock(mutex);
            auto it = entries.find(key);
            if (it != entries.end())
            {
                // if (c_fileLogLevel >= bcos::LogLevel::TRACE)
                // {  // FIXME: this log is only for debug, comment it when release
                //     KeyPage_LOG(TRACE)
                //         << LOG_DESC("getEntry") << LOG_KV("pageKey", toHex(entries.begin()->first))
                //         << LOG_KV("valid", m_validCount) << LOG_KV("count", entries.size())
                //         << LOG_KV("key", toHex(key)) << LOG_KV("status", (int)it->second.status());
                // }
                if (it->second.status() != Entry::Status::DELETED &&
                    it->second.status() != Entry::EMPTY)
                {
                    return std::make_optional(it->second);
                }
            }
            else
            {
                // if (c_fileLogLevel >= bcos::LogLevel::TRACE)
                // {  // FIXME: this log is only for debug, comment it when release
                //     KeyPage_LOG(TRACE)
                //         << LOG_DESC("getEntry not found")
                //         << LOG_KV(
                //                "pageKey", entries.empty() ? "empty" : toHex(entries.begin()->first))
                //         << LOG_KV("valid", m_validCount) << LOG_KV("count", entries.size())
                //         << LOG_KV("key", toHex(key));
                // }
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
        {  // TODO: do not exist entry optimization: insert a empty entry to cache, then
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
                        ++m_validCount;
                        pageInfoChanged = true;
                    }
                }
                else
                {
                    entry.set(std::string());
                    entry.setStatus(Entry::Status::DELETED);
                    if (it->second.status() != Entry::Status::DELETED)
                    {
                        --m_validCount;
                        pageInfoChanged = true;
                    }
                }
                ret = std::move(it->second);
                it->second = std::move(entry);
                if (c_fileLogLevel >= bcos::LogLevel::TRACE)
                {  // FIXME: this log is only for debug, comment it when release
                    KeyPage_LOG(TRACE)
                        << LOG_DESC("setEntry update")
                        << LOG_KV("pageKey", toHex(entries.begin()->first))
                        << LOG_KV("valid", m_validCount) << LOG_KV("count", entries.size())
                        << LOG_KV("key", toHex(key)) << LOG_KV("status", (int)it->second.status());
                }
            }
            else
            {
                if (entry.status() != Entry::Status::DELETED)
                {
                    ++m_validCount;
                    m_size += entry.size();
                    pageInfoChanged = true;
                }
                if (entries.size() == 0 || key < entries.begin()->first ||
                    key > entries.rbegin()->first)
                {
                    pageInfoChanged = true;
                    if (key < entries.begin()->first)
                    {
                        m_invalidStartKeys.insert(entries.begin()->first);
                    }
                }
                entries[std::string(key)] = std::move(entry);
                if (c_fileLogLevel >= bcos::LogLevel::TRACE)
                {  // FIXME: this log is only for debug, comment it when release
                    KeyPage_LOG(TRACE) << LOG_DESC("setEntry insert")
                                       << LOG_KV("startKey", toHex(entries.begin()->first))
                                       << LOG_KV("key", toHex(key)) << LOG_KV("valid", m_validCount)
                                       << LOG_KV("count", entries.size())
                                       << LOG_KV("pageInfoChanged", pageInfoChanged)
                                       << LOG_KV("status", (int)entry.status());
                }
            }
            return std::make_tuple(ret, pageInfoChanged);
        }
        size_t size() const
        {
            std::shared_lock lock(mutex);
            return m_size;
        }
        size_t validCount() const
        {
            std::shared_lock lock(mutex);
            return m_validCount;
        }
        size_t count() const
        {
            std::shared_lock lock(mutex);
            return entries.size();
        }
        std::set<std::string> invalidKeySet() const
        {
            std::shared_lock lock(mutex);
            return m_invalidStartKeys;
        }
        std::string startKey() const
        {
            std::shared_lock lock(mutex);
            if (entries.empty())
            {
                return "";
            }
            return entries.begin()->first;
        }
        std::string endKey() const
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
            Page page;
            std::unique_lock lock(mutex);
            // split this page to two pages
            auto iter = entries.begin();
            while (iter != entries.end())
            {
                m_size -= iter->second.size();
                page.m_size += iter->second.size();
                if (iter->second.status() != Entry::Status::DELETED)
                {
                    --m_validCount;
                    ++page.m_validCount;
                }
                if (!m_invalidStartKeys.empty() && iter->first == *m_invalidStartKeys.begin())
                {
                    page.m_invalidStartKeys.insert(
                        m_invalidStartKeys.extract(m_invalidStartKeys.begin()));
                }
                auto ret = page.entries.insert(entries.extract(iter));
                assert(ret.inserted);
                iter = entries.begin();
                // m_invalidStartKeys
                if (m_size - iter->second.size() < threshold)
                {
                    break;
                }
            }
            if (!m_invalidStartKeys.empty() && entries.begin()->first == *m_invalidStartKeys.begin())
            {  // if new pageKey has been pageKey, remove it from m_invalidStartKeys
                m_invalidStartKeys.erase(m_invalidStartKeys.begin());
            }
            return page;
        }

        void merge(Page& p)
        {
            assert(this != &p);
            if (this != &p)
            {
                std::unique_lock lock(mutex);
                for (auto iter = p.entries.begin(); iter != p.entries.end();)
                {
                    m_size += iter->second.size();
                    p.m_size -= iter->second.size();
                    if (iter->second.status() != Entry::Status::DELETED)
                    {
                        ++m_validCount;
                        --p.m_validCount;
                    }
                    entries.insert(p.entries.extract(iter));
                    iter = p.entries.begin();
                }
                m_invalidStartKeys.merge(p.m_invalidStartKeys);
            }
            else
            {
                KeyPage_LOG(FATAL)
                    << LOG_DESC("merge slef") << LOG_KV("startKey", toHex(entries.begin()->first))
                    << LOG_KV("valid", m_validCount) << LOG_KV("count", entries.size());
            }
        }
        void clean()
        {
            std::unique_lock lock(mutex);
            for (auto iter = entries.begin(); iter != entries.end();)
            {
                if (iter->second.status() != Entry::Status::DELETED)
                {
                    iter->second.setStatus(Entry::Status::NORMAL);
                    ++iter;
                }
                else
                {
                    iter = entries.erase(iter);
                }
            }
        }
        crypto::HashType hash(
            const std::string& table, const bcos::crypto::Hash::Ptr& hashImpl) const
        {
            bcos::crypto::HashType pageHash(0);
            std::shared_lock lock(mutex);
            auto hash = hashImpl->hash(table);
            for (auto iter = entries.cbegin(); iter != entries.cend(); ++iter)
            {
                if (iter->second.dirty())
                {
                    auto entryHash = hash ^ hashImpl->hash(iter->first) ^
                                     iter->second.hash(table, iter->first, hashImpl);
                    if (c_fileLogLevel >= TRACE)
                    {
                        STORAGE_LOG(TRACE)
                            << "Storage hash: " << LOG_KV("table", table)
                            << LOG_KV("key", toHex(iter->first)) << LOG_KV("hash", entryHash.hex());
                    }
                    pageHash ^= entryHash;
                }
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
                    if (it->second.status() == Entry::Status::DELETED &&
                        change.entry->status() != Entry::Status::DELETED)
                    {
                        ++m_validCount;
                    }
                    else if (it->second.status() != Entry::Status::DELETED &&
                             change.entry->status() == Entry::Status::DELETED)
                    {
                        --m_validCount;
                    }
                    m_size -= it->second.size();
                    it->second = std::move(*change.entry);
                    m_size += it->second.size();
                }
                else
                {  // delete, should not happen?
                    if (c_fileLogLevel >= bcos::LogLevel::TRACE)
                    {
                        KeyPage_LOG(TRACE)
                            << "Revert delete: " << change.table << " | " << toHex(change.key)
                            << " | " << toHex(change.entry->get());
                    }
                    if (change.entry->status() != Entry::Status::DELETED)
                    {
                        ++m_validCount;
                    }
                    m_size -= change.entry->size();
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
                    if (it->second.status() != Entry::Status::DELETED)
                    {
                        --m_validCount;
                    }
                    entries.erase(it);
                    if (!m_invalidStartKeys.empty() && entries.begin()->first == *m_invalidStartKeys.begin())
                    {  // if new pageKey has been pageKey, remove it from m_invalidStartKeys
                        m_invalidStartKeys.erase(m_invalidStartKeys.begin());
                    }
                }
                else
                {  // delete a key not exist
                    KeyPage_LOG(DEBUG)
                        << "Revert invalid delete: " << change.table << " | " << toHex(change.key);
                    auto message = (boost::format("Not found rollback entry: %s:%s") %
                                    change.table % change.key)
                                       .str();
                    BOOST_THROW_EXCEPTION(BCOS_ERROR(StorageError::UnknownError, message));
                }
            }
        }
        std::unique_lock<std::shared_mutex> lock() { return std::unique_lock(mutex); }
        std::shared_lock<std::shared_mutex> rLock() { return std::shared_lock(mutex); }

    private:
        //   PageInfo* pageInfo;
        mutable std::shared_mutex mutex;
        std::map<std::string, Entry, std::less<>> entries;
        uint32_t m_size = 0;        // page real size
        uint32_t m_validCount = 0;  // valid entry count
        friend class boost::serialization::access;
        // if startKey changed the old startKey need keep to delete old page
        std::set<std::string> m_invalidStartKeys;
        template <class Archive>
        void save(Archive& ar, const unsigned int version) const
        {
            std::ignore = version;
            ar&(uint32_t)m_validCount;
            size_t count = 0;
            for (auto& i : entries)
            {
                if (i.second.status() == Entry::Status::DELETED)
                {  // skip deleted entry
                    continue;
                }
                ++count;
                ar& i.first;
                auto value = i.second.get();
                ar&(uint32_t)value.size();
                for (size_t i = 0; i < value.size(); ++i)
                {
                    ar&(uint8_t)value[i];
                }
            }
            assert(count == m_validCount);
        }
        template <class Archive>
        void load(Archive& ar, const unsigned int version)
        {
            std::ignore = version;
            uint32_t count = 0;
            ar& count;
            m_validCount = count;
            for (size_t i = 0; i < m_validCount; ++i)
            {
                std::string key;
                ar& key;
                uint32_t len = 0;
                ar& len;
                m_size += len;
                std::vector<uint8_t> value(len, 0);
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
        Data(std::string _table, std::string _key, Entry _entry, Type _type)
          : table(std::move(_table)), key(std::move(_key)), type(_type), entry(std::move(_entry))
        {
            if (type == Type::TableMeta)
            {
                auto meta = KeyPageStorage::TableMeta(entry.get());
                if (c_fileLogLevel >= TRACE)
                {
                    KeyPage_LOG(TRACE)
                        << LOG_DESC("Data TableMeta") << LOG_KV("table", table)
                        << LOG_KV("len", entry.size()) << LOG_KV("size", meta.size());
                }
                data = std::move(meta);
            }
            else if (type == Type::Page)
            {
                auto page = KeyPageStorage::Page(entry.get());
                if (c_fileLogLevel >= TRACE)
                {
                    KeyPage_LOG(TRACE)
                        << LOG_DESC("Data Page") << LOG_KV("table", table)
                        << LOG_KV("key", toHex(key)) << LOG_KV("startKey", toHex(page.startKey()))
                        << LOG_KV("endKey", toHex(page.endKey())) << LOG_KV("len", entry.size())
                        << LOG_KV("valid", page.validCount()) << LOG_KV("count", page.count())
                        << LOG_KV("pageSize", page.size());
                }
                data = std::move(page);
            }
            else
            {  // sys table entry
            }
        };
        Data(const Data& d) = default;
        Data& operator=(const Data& d) = default;
        Data(Data&& d) = default;
        Data& operator=(Data&& d) = default;
        std::string table;
        std::string key;
        Type type = Type::NormalEntry;
        Entry entry;
        std::variant<KeyPageStorage::Page, KeyPageStorage::TableMeta> data;
        // std::pair<std::string_view, std::string_view> view() const
        // {
        //     return std::make_pair(std::string_view(table), std::string_view(key));
        // }
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

    std::optional<Data> copyData(std::string_view table, std::string_view key)
    {
        auto [bucket, lock] = getBucket(table, key);
        boost::ignore_unused(lock);
        auto it = bucket->container.find(std::make_pair(std::string(table), std::string(key)));
        if (it != bucket->container.end())
        {
            return std::make_optional(it->second);
        }
        auto prevKeyPage = std::dynamic_pointer_cast<bcos::storage::KeyPageStorage>(getPrev());
        if (prevKeyPage)
        {
            return prevKeyPage->copyData(table, key);
        }
        auto [error, entry] = getRawEntryFromStorage(table, key);
        if (error)
        {
            KeyPage_LOG(ERROR) << LOG_DESC("getData error") << LOG_KV("table", table)
                               << LOG_KV("key", toHex(key))
                               << LOG_KV("error", error->errorMessage());
            return std::nullopt;
        }
        KeyPage_LOG(TRACE) << LOG_DESC("get data from storage") << LOG_KV("table", table)
                           << LOG_KV("key", toHex(key)) << LOG_KV("found", entry ? true : false);
        if (entry)
        {
            entry->setStatus(Entry::Status::NORMAL);
            return std::make_optional(Data(std::string(table), std::string(key), std::move(*entry),
                key.empty() ? Data::Type::TableMeta : Data::Type::Page));
        }
        return std::nullopt;
    }

private:
    std::shared_ptr<StorageInterface> getPrev()
    {
        std::shared_lock<std::shared_mutex> lock(m_prevMutex);
        return m_prev;
    }
    size_t getBucketIndex(std::string_view table, std::string_view key) const
    {
        auto hash = std::hash<std::string_view>{}(table);
        std::ignore = key;
        // boost::hash_combine(hash, std::hash<std::string_view>{}(key));
        return hash % m_buckets.size();
    }

    Data* changePageKey(std::string table, std::string oldStartKey, std::string newStartKey)
    {
        auto [bucket, lock] = getMutBucket(table, oldStartKey);
        boost::ignore_unused(lock);
        auto n = bucket->container.extract(std::make_pair(table, oldStartKey));
        KeyPage_LOG(DEBUG) << LOG_DESC("changePageKey") << LOG_KV("table", table)
                           << LOG_KV("oldStartKey", toHex(oldStartKey))
                           << LOG_KV("newStartKey", toHex(newStartKey));
        n.key().second = newStartKey;
        n.mapped().key = newStartKey;
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

    void insertNewPage(std::string_view tableView, std::string_view keyView,
        KeyPageStorage::TableMeta& meta, Page&& page)
    {
        // prepare page info
        PageInfo info;
        info.startKey = page.startKey();
        info.endKey = page.endKey();
        info.count = page.validCount();
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
        bucket->container.insert_or_assign(
            std::make_pair(std::string(tableView), std::string(keyView)), std::move(d));
        lock.unlock();
        // update table meta
        meta.insertPageInfoNoLock(std::move(info));
    }

    std::pair<Error::UniquePtr, std::optional<Entry>> getSysTableRawEntry(
        std::string_view table, std::string_view key);
    std::pair<Error::UniquePtr, std::optional<Entry>> getRawEntryFromStorage(
        std::string_view table, std::string_view key);
    Entry importExistingEntry(std::string_view table, std::string_view key, Entry entry);

    // if data not exist, create an empty one
    std::tuple<Error::UniquePtr, std::optional<Data*>> getData(
        std::string_view tableView, std::string_view key, bool mustExist = false);
    std::pair<Error::UniquePtr, std::optional<Entry>> getEntryFromPage(
        std::string_view table, std::string_view key);
    Error::UniquePtr setEntryToPage(std::string table, std::string key, Entry entry);

    size_t m_pageSize = 8 * 1024;
    size_t m_splitSize;
    size_t m_mergeSize;
    std::vector<Bucket> m_buckets;
};

}  // namespace bcos::storage
