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
#include <boost/serialization/vector.hpp>
#include <cstddef>
#include <exception>
#include <memory>
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
const size_t MIN_PAGE_SIZE = 2048;
class KeyPageStorage : public virtual storage::StateStorageInterface
{
public:
    using Ptr = std::shared_ptr<KeyPageStorage>;

    explicit KeyPageStorage(std::shared_ptr<StorageInterface> _prev, size_t _pageSize = 1024)
      : storage::StateStorageInterface(_prev),
        m_pageSize(_pageSize > MIN_PAGE_SIZE ? _pageSize : MIN_PAGE_SIZE),
        m_splitSize(m_pageSize / 3 * 2),
        m_mergeSize(m_pageSize / 4),
        m_buckets(std::thread::hardware_concurrency())
    {}

    KeyPageStorage(const KeyPageStorage&) = delete;
    KeyPageStorage& operator=(const KeyPageStorage&) = delete;

    KeyPageStorage(KeyPageStorage&&) = delete;
    KeyPageStorage& operator=(KeyPageStorage&&) = delete;

    virtual ~KeyPageStorage()
    {
        m_recoder.clear();
        // #pragma omp parallel for
        for (size_t i = 0; i < m_buckets.size(); ++i)
        {
            m_buckets[i].container.clear();
        }
    }

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

    struct Data;
    class PageInfo
    {  // all methods is not thread safe
    public:
        bool operator<(const PageInfo& rhs) const { return m_data->pageKey < rhs.m_data->pageKey; }
        PageInfo() {}
        PageInfo(std::string _pageKey, uint16_t _count, uint16_t _size, Data* p)
          : m_data(std::make_shared<PageInfoData>(std::move(_pageKey), _count, _size)),
            m_pageData(p)
        {}
        PageInfo(PageInfo&&) = default;
        PageInfo& operator=(PageInfo&&) = default;
        PageInfo(const PageInfo& p) = default;
        PageInfo& operator=(const PageInfo& p) = default;

        void setPageKey(std::string key)
        {
            prepareMyData();
            m_data->pageKey = std::move(key);
        }
        std::string getPageKey() const { return m_data->pageKey; }
        void setCount(uint16_t _count)
        {
            prepareMyData();
            m_data->count = _count;
        }
        uint16_t getCount() const { return m_data->count; }
        void setSize(uint16_t _size)
        {
            prepareMyData();
            m_data->size = _size;
        }
        uint16_t getSize() const { return m_data->size; }

        Data* getPageData() const { return m_pageData; }
        void setPageData(Data* p) { m_pageData = p; }

    private:
        void prepareMyData()
        {
            if (m_data.use_count() > 1)
            {
                m_data =
                    std::make_shared<PageInfoData>(m_data->pageKey, m_data->count, m_data->size);
            }
        }
        struct PageInfoData
        {
            PageInfoData() {}
            PageInfoData(std::string _endKey, uint16_t _count, uint16_t _size)
              : pageKey(std::move(_endKey)), count(_count), size(_size)
            {}
            // startKey is not involved in comparison
            std::string pageKey;
            uint16_t count = 0;
            uint16_t size = 0;
        };
        std::shared_ptr<PageInfoData> m_data = nullptr;
        Data* m_pageData = nullptr;
        friend class boost::serialization::access;

        template <class Archive>
        void save(Archive& ar, const unsigned int version) const
        {
            std::ignore = version;
            ar & m_data->pageKey;
            ar & m_data->count;
            ar & m_data->size;
        }
        template <class Archive>
        void load(Archive& ar, const unsigned int version)
        {
            std::ignore = version;
            m_data = std::make_shared<PageInfoData>();
            ar & m_data->pageKey;
            ar & m_data->count;
            ar & m_data->size;
        }
        BOOST_SERIALIZATION_SPLIT_MEMBER()
    };

    struct TableMeta
    {
        TableMeta() : pages(std::make_unique<std::vector<PageInfo>>()) {}
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
        TableMeta(const TableMeta& t)
        {
            pages = std::make_unique<std::vector<PageInfo>>();
            *pages = *t.pages;
        }
        TableMeta& operator=(const TableMeta& t)
        {
            if (this != &t)
            {
                pages = std::make_unique<std::vector<PageInfo>>();
                *pages = *t.pages;
            }
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
            return std::lower_bound(pages->begin(), pages->end(), key,
                [](const PageInfo& lhs, const std::string_view& rhs) {
                    return lhs.getPageKey() < rhs;
                });
        }
        auto upper_bound(std::string_view key)
        {
            return std::upper_bound(pages->begin(), pages->end(), key,
                [](const std::string_view& lhs, const PageInfo& rhs) {
                    return lhs < rhs.getPageKey();
                });
        }
        inline std::optional<PageInfo*> getPageInfoNoLock(std::string_view key)
        {
            count += 1;
            if (pages->empty())
            {  // if pages is empty
                return std::nullopt;
            }
            if (lastPageInfoIndex < pages->size())
            {
                auto lastPageInfo = &pages->at(lastPageInfoIndex);
                if (lastPageInfo->getPageData())
                {
                    auto page = &std::get<0>(lastPageInfo->getPageData()->data);
                    if (page->startKey() <= key && key <= page->endKey())
                    {
                        hit += 1;
                        return lastPageInfo;
                    }
                }
            }
            auto it = lower_bound(key);
            if (it != pages->end())
            {
                lastPageInfoIndex = it - pages->begin();
                return &*it;
            }
            if (pages->rbegin()->getPageKey().empty())
            {  // page is empty because of rollback
                return std::nullopt;
            }
            lastPageInfoIndex = pages->size() - 1;
            return &pages->back();
        }

        std::optional<std::string> getNextPageKeyNoLock(std::string_view key)
        {
            if (key.empty())
            {
                return std::nullopt;
            }
            auto it = upper_bound(key);
            if (it != pages->end())
            {
                return it->getPageKey();
            }
            return std::nullopt;
        }

        std::vector<PageInfo>& getAllPageInfoNoLock() { return std::ref(*pages); }
        void insertPageInfo(PageInfo&& pageInfo)
        {
            std::unique_lock lock(mutex);
            insertPageInfoNoLock(std::move(pageInfo));
        }
        void insertPageInfoNoLock(PageInfo&& pageInfo)
        {
            assert(!pageInfo.getPageKey().empty());
            if (pageInfo.getPageKey().empty())
            {
                KeyPage_LOG(FATAL) << LOG_DESC("insert empty pageInfo");
                return;
            }
            auto it = lower_bound(pageInfo.getPageKey());
            auto newIt = pages->insert(it, std::move(pageInfo));
            if (c_fileLogLevel >= TRACE)
            {
                KeyPage_LOG(TRACE)
                    << LOG_DESC("insert new pageInfo")
                    << LOG_KV("pageKey", toHex(newIt->getPageKey()))
                    << LOG_KV("valid", newIt->getCount()) << LOG_KV("size", newIt->getSize());
            }
        }

        std::optional<std::string> updatePageInfoNoLock(std::string_view oldEndKey,
            const std::string& pageKey, size_t count, size_t size,
            std::optional<PageInfo*> pageInfo)
        {
            std::optional<std::string> oldPageKey;
            auto updateInfo = [&](PageInfo* p) {
                if (p->getPageKey() != pageKey)
                {
                    oldPageKey = p->getPageKey();
                    p->setPageKey(pageKey);
                    if (c_fileLogLevel >= TRACE)
                    {
                        KeyPage_LOG(TRACE) << LOG_DESC("updatePageInfo")
                                           << LOG_KV("oldPageKey", toHex(oldPageKey.value()))
                                           << LOG_KV("newPageKey", toHex(pageKey))
                                           << LOG_KV("count", count) << LOG_KV("size", size);
                    }
                }
                p->setCount(count);
                p->setSize(size);
            };
            if (pageInfo.has_value())
            {
                updateInfo(pageInfo.value());
            }
            else
            {
                auto it = lower_bound(oldEndKey);
                if (it != pages->end())
                {
                    updateInfo(&*it);
                }
                else
                {
                    assert(false);
                    KeyPage_LOG(ERROR)
                        << LOG_DESC("updatePageInfo not found")
                        << LOG_KV("oldEndKey", toHex(oldEndKey)) << LOG_KV("endKey", toHex(pageKey))
                        << LOG_KV("valid", count) << LOG_KV("size", size);
                }
            }
            return oldPageKey;
        }

        std::optional<PageInfo*> deletePageInfoNoLock(
            std::string_view endkey, std::optional<PageInfo*> pageInfo)
        {  // remove current page info and update next page start key
            std::vector<PageInfo>::iterator it;
            if (pageInfo)
            {
                auto offset = pageInfo.value() - &*pages->begin();
                it = pages->begin() + offset;
            }
            else
            {
                it = lower_bound(endkey);
            }
            if (it != pages->end() && it->getPageKey() == endkey)
            {
                return &*pages->erase(it);
            }
            return std::nullopt;
        }
        size_t size() const
        {
            std::shared_lock lock(mutex);
            return pages->size();
        }

        void clean()
        {
            for (auto& p : *pages)
            {
                p.setPageData(nullptr);
            }
        }
        std::unique_lock<std::shared_mutex> lock() const { return std::unique_lock(mutex); }
        std::shared_lock<std::shared_mutex> rLock() const { return std::shared_lock(mutex); }

        friend std::ostream& operator<<(
            std::ostream& os, const bcos::storage::KeyPageStorage::TableMeta& meta)
        {
            auto lock = std::shared_lock(meta.mutex);
            os << "[";
            for (auto& pageInfo : *meta.pages)
            {
                os << "{"
                   //    << "startKey=" << toHex(pageInfo.getStartKey())
                   << ",endKey=" << toHex(pageInfo.getPageKey()) << ",count=" << pageInfo.getCount()
                   << ",size=" << pageInfo.getSize() << "}";
            }
            os << "]";
            return os;
        }
        double hitRate() { return hit / (double)count; }

    private:
        uint32_t count = 0;
        uint32_t hit = 0;
        mutable std::shared_mutex mutex;
        std::unique_ptr<std::vector<PageInfo>> pages = nullptr;
        friend class boost::serialization::access;
        size_t lastPageInfoIndex = 0;
        template <class Archive>
        void save(Archive& ar, const unsigned int version) const
        {
            std::ignore = version;
            // auto len = (uint32_t)pages->size();
            // ar & len;
            // for (size_t i = 0; i < pages->size(); ++i)
            // {
            //     ar & pages->at(i);
            // }
            ar << *pages;
        }
        template <class Archive>
        void load(Archive& ar, const unsigned int version)
        {
            std::ignore = version;
            // uint32_t s = 0;
            // ar & s;
            // pages = std::make_unique<std::vector<PageInfo>>(s, PageInfo());
            // for (size_t i = 0; i < pages->size(); ++i)
            // {
            //     ar & pages->at(i);
            // }
            pages = std::make_unique<std::vector<PageInfo>>();
            ar >> *pages;
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
            m_invalidPageKeys = p.m_invalidPageKeys;
        }
        Page& operator=(const Page& p)
        {
            if (this != &p)
            {
                entries = p.entries;
                m_size = p.m_size;
                m_validCount = p.m_validCount;
                m_invalidPageKeys = p.m_invalidPageKeys;
            }
            return *this;
        }
        Page(Page&& p)
        {
            entries = std::move(p.entries);
            m_size = p.m_size;
            m_validCount = p.m_validCount;
            m_invalidPageKeys = std::move(p.m_invalidPageKeys);
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
                //         << LOG_DESC("getEntry") << LOG_KV("pageKey",
                //         toHex(entries.begin()->first))
                //         << LOG_KV("valid", m_validCount) << LOG_KV("count", entries.size())
                //         << LOG_KV("key", toHex(key)) << LOG_KV("status",
                //         (int)it->second.status());
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
                //                "pageKey", entries.empty() ? "empty" :
                //                toHex(entries.begin()->first))
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
        inline std::tuple<std::optional<Entry>, bool> setEntry(
            const std::string_view& key, Entry&& entry)
        {  // TODO: do not exist entry optimization: insert a empty entry to cache, then
            // entry status none should return null optional
            bool pageInfoChanged = false;
            std::optional<Entry> ret;
            std::unique_lock lock(mutex);
            auto it = entries.lower_bound(key);
            m_size += entry.size();
            if (it != entries.end() && it->first == key)
            {  // delete exist entry
                m_size -= it->second.size();
                if (entry.status() != Entry::Status::DELETED)
                {
                    if (it->second.status() == Entry::Status::DELETED)
                    {
                        ++m_validCount;
                        pageInfoChanged = true;
                    }
                }
                else
                {
                    if (it->second.status() != Entry::Status::DELETED)
                    {
                        --m_validCount;
                        pageInfoChanged = true;
                    }
                }
                ret = std::move(it->second);
                it->second = std::move(entry);
                // if (c_fileLogLevel >= bcos::LogLevel::TRACE)
                // {  // FIXME: this log is only for debug, comment it when release
                //     KeyPage_LOG(TRACE)
                //         << LOG_DESC("setEntry update")
                //         << LOG_KV("pageKey", toHex(entries.begin()->first))
                //         << LOG_KV("valid", m_validCount) << LOG_KV("count", entries.size())
                //         << LOG_KV("key", toHex(key)) << LOG_KV("status",
                //         (int)it->second.status());
                // }
            }
            else
            {
                m_size += key.size();
                if (entry.status() != Entry::Status::DELETED)
                {
                    ++m_validCount;
                    pageInfoChanged = true;
                }
                if (entries.empty() || key > entries.rbegin()->first)
                {
                    pageInfoChanged = true;
                    if (!entries.empty())
                    {  // means key > entries.rbegin()->first is true
                        m_invalidPageKeys.emplace_back(entries.rbegin()->first);
                    }
                }
                entries.insert(it, std::make_pair(std::string(key), std::move(entry)));
                // if (c_fileLogLevel >= bcos::LogLevel::TRACE)
                // {  // FIXME: this log is only for debug, comment it when release
                //     KeyPage_LOG(TRACE) << LOG_DESC("setEntry insert")
                //                        << LOG_KV("startKey", toHex(entries.begin()->first))
                //                        << LOG_KV("key", toHex(key)) << LOG_KV("valid",
                //                        m_validCount)
                //                        << LOG_KV("count", entries.size())
                //                        << LOG_KV("pageInfoChanged", pageInfoChanged)
                //                        << LOG_KV("status", (int)entry.status());
                // }
            }
            return std::make_tuple(std::move(ret), pageInfoChanged);
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
        std::list<std::string> invalidKeySet() const
        {
            std::shared_lock lock(mutex);
            return m_invalidPageKeys;
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
        auto split(size_t threshold)
        {
            auto page = Page();
            std::unique_lock lock(mutex);
            // split this page to two pages
            auto iter = entries.begin();
            while (iter != entries.end())
            {
                m_size -= iter->second.size();
                m_size -= iter->first.size();
                page.m_size += iter->second.size();
                page.m_size += iter->first.size();
                if (iter->second.status() != Entry::Status::DELETED)
                {
                    --m_validCount;
                    ++page.m_validCount;
                }
                if (!m_invalidPageKeys.empty() && iter->first == *m_invalidPageKeys.begin())
                {
                    page.m_invalidPageKeys.splice(
                        page.m_invalidPageKeys.end(), m_invalidPageKeys, m_invalidPageKeys.begin());
                }
                auto ret = page.entries.insert(entries.extract(iter));
                assert(ret.inserted);
                iter = entries.begin();
                if (m_size - iter->second.size() < threshold || entries.size() == 1)
                {
                    break;
                }
            }
            if (!page.m_invalidPageKeys.empty() &&
                page.entries.rbegin()->first == *page.m_invalidPageKeys.rbegin())
            {  // if new pageKey has been pageKey, remove it from m_invalidPageKeys
                page.m_invalidPageKeys.pop_back();
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
                    m_size += iter->first.size();
                    p.m_size -= iter->second.size();
                    p.m_size -= iter->first.size();
                    if (iter->second.status() != Entry::Status::DELETED)
                    {
                        ++m_validCount;
                        --p.m_validCount;
                    }
                    entries.insert(p.entries.extract(iter));
                    iter = p.entries.begin();
                }
                m_invalidPageKeys.merge(p.m_invalidPageKeys);
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
            m_invalidPageKeys.clear();
        }
        crypto::HashType hash(
            const std::string& table, const bcos::crypto::Hash::Ptr& hashImpl) const
        {
            bcos::crypto::HashType pageHash(0);
            auto hash = hashImpl->hash(table);
            // std::shared_lock lock(mutex);
            for (auto iter = entries.cbegin(); iter != entries.cend(); ++iter)
            {
                if (iter->second.dirty())
                {
                    auto entryHash = hash ^ hashImpl->hash(iter->first) ^
                                     iter->second.hash(table, iter->first, hashImpl);
                    // if (c_fileLogLevel >= TRACE)
                    // {
                    //     STORAGE_LOG(TRACE)
                    //         << "Storage hash: " << LOG_KV("table", table)
                    //         << LOG_KV("key", toHex(iter->first)) << LOG_KV("hash",
                    //         entryHash.hex());
                    // }
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
                    m_size -= it->first.size();
                    if (it->second.status() != Entry::Status::DELETED)
                    {
                        --m_validCount;
                    }
                    entries.erase(it);
                    if (!m_invalidPageKeys.empty() && !entries.empty() &&
                        entries.rbegin()->first == *m_invalidPageKeys.rbegin())
                    {  // if new pageKey has been pageKey, remove it from m_invalidPageKeys
                        m_invalidPageKeys.pop_back();
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
        std::list<std::string> m_invalidPageKeys;
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
                ar.save_binary(value.data(), value.size());
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
            auto iter = entries.begin();
            for (size_t i = 0; i < m_validCount; ++i)
            {
                std::string key;
                ar& key;
                uint32_t len = 0;
                ar& len;
                m_size += len;
                m_size += key.size();
                auto value = std::make_shared<std::vector<uint8_t>>(len, 0);
                ar.load_binary(value->data(), value->size());
                Entry e;
                e.setPointer(std::move(value));
                e.setStatus(Entry::Status::NORMAL);
                iter = entries.emplace_hint(iter, std::move(key), std::move(e));
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
        std::unordered_map<std::pair<std::string, std::string>, std::shared_ptr<Data>> container;
        std::shared_mutex mutex;
        std::optional<Data*> find(std::string_view table, std::string_view key)
        {
            std::shared_lock lock(mutex);
            auto it = container.find(std::make_pair(std::string(table), std::string(key)));
            if (it != container.end())
            {
                return it->second.get();
            }
            return std::nullopt;
        }
        std::unique_lock<std::shared_mutex> lock() { return std::unique_lock(mutex); }
        std::shared_lock<std::shared_mutex> rLock() { return std::shared_lock(mutex); }
    };

    std::optional<std::shared_ptr<Data>> copyData(std::string_view table, std::string_view key)
    {
        auto [bucket, lock] = getBucket(table, key);
        boost::ignore_unused(lock);
        auto it = bucket->container.find(std::make_pair(std::string(table), std::string(key)));
        if (it != bucket->container.end())
        {
            return std::make_optional(std::make_shared<Data>(*it->second));
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
        if (c_fileLogLevel >= TRACE)
        {
            KeyPage_LOG(TRACE) << LOG_DESC("get data from storage") << LOG_KV("table", table)
                               << LOG_KV("key", toHex(key))
                               << LOG_KV("found", entry ? true : false);
        }
        if (entry)
        {
            entry->setStatus(Entry::Status::NORMAL);
            return std::make_optional(std::make_shared<Data>(std::string(table), std::string(key),
                std::move(*entry), key.empty() ? Data::Type::TableMeta : Data::Type::Page));
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
        // the table must in one bucket
        // boost::hash_combine(hash, std::hash<std::string_view>{}(key));
        return hash % m_buckets.size();
    }

    Data* changePageKey(
        std::string table, const std::string& oldPageKey, const std::string& newPageKey)
    {
        if (newPageKey.empty())
        {
            KeyPage_LOG(ERROR) << LOG_DESC("changePageKey empty page") << LOG_KV("table", table)
                               << LOG_KV("oldPageKey", toHex(oldPageKey))
                               << LOG_KV("newPageKey", toHex(newPageKey));
            return nullptr;
        }
        auto [bucket, lock] = getMutBucket(table, oldPageKey);
        boost::ignore_unused(lock);
        auto n = bucket->container.extract(std::make_pair(table, oldPageKey));
        KeyPage_LOG(DEBUG) << LOG_DESC("changePageKey") << LOG_KV("table", table)
                           << LOG_KV("oldPageKey", toHex(oldPageKey))
                           << LOG_KV("newPageKey", toHex(newPageKey));
        n.key().second = newPageKey;
        n.mapped()->key = newPageKey;
        if (newPageKey.empty())
        {
            return nullptr;
        }
        auto it = bucket->container.find(std::make_pair(table, newPageKey));
        if (it != bucket->container.end())
        {  // erase old page to update data
            bucket->container.erase(it);
        }
        auto ret = bucket->container.insert(std::move(n));
        assert(ret.inserted);
        return ret.position->second.get();
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
        KeyPageStorage::TableMeta* meta, Page&& page)
    {
        // insert page
        auto d = std::make_shared<Data>();
        d->table = std::string(tableView);
        d->key = std::string(keyView);
        d->type = Data::Type::Page;
        // prepare page info
        PageInfo info(page.endKey(), page.validCount(), page.size(), d.get());

        d->data = std::move(page);

        // d.entry.setDirty(true);
        d->entry.setStatus(Entry::Status::MODIFIED);
        auto [bucket, lock] = getMutBucket(tableView, keyView);
        boost::ignore_unused(lock);
        bucket->container.insert_or_assign(
            std::make_pair(std::string(tableView), std::string(keyView)), std::move(d));
        lock.unlock();
        // update table meta
        meta->insertPageInfoNoLock(std::move(info));
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
