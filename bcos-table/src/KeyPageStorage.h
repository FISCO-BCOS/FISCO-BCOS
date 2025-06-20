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

#include "StateStorageInterface.h"
#include <boost/archive/basic_archive.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/core/ignore_unused.hpp>
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
#include <atomic>
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
    auto operator()(const std::pair<std::string_view, std::string_view>& p) const -> size_t
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
    auto operator()(const std::pair<std::string, std::string>& p) const -> size_t
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
const char* const TABLE_META_KEY = "";
const size_t MIN_PAGE_SIZE = 2048;
class KeyPageStorage : public virtual storage::StateStorageInterface
{
public:
    using Ptr = std::shared_ptr<KeyPageStorage>;

    explicit KeyPageStorage(std::shared_ptr<StorageInterface> _prev, bool setRowWithDirtyFlag,
        size_t _pageSize = 10240,
        uint32_t _blockVersion = (uint32_t)bcos::protocol::BlockVersion::V3_0_VERSION,
        std::shared_ptr<const std::set<std::string, std::less<>>> _ignoreTables = nullptr,
        bool _ignoreNotExist = false)
      : storage::StateStorageInterface(std::move(_prev)),
        m_blockVersion(_blockVersion),
        m_pageSize(_pageSize > MIN_PAGE_SIZE ? _pageSize : MIN_PAGE_SIZE),
        m_splitSize(m_pageSize / 3 * 2),
        m_mergeSize(m_pageSize / 4),
        m_buckets(std::thread::hardware_concurrency()),
        m_ignoreTables(std::move(_ignoreTables)),
        m_ignoreNotExist(_ignoreNotExist),
        m_setRowWithDirtyFlag(setRowWithDirtyFlag)
    {
        if (!m_ignoreTables)
        {
            auto ignore = std::make_shared<std::set<std::string, std::less<>>>();
            ignore->insert(std::string(SYS_TABLES));
            m_ignoreTables = ignore;
        }
    }

    KeyPageStorage(const KeyPageStorage&) = delete;
    KeyPageStorage& operator=(const KeyPageStorage&) = delete;

    KeyPageStorage(KeyPageStorage&&) = delete;
    KeyPageStorage& operator=(KeyPageStorage&&) = delete;

    ~KeyPageStorage() override
    {
        m_recoder.clear();
        m_buckets.clear();
    }

    void asyncGetPrimaryKeys(std::string_view table,
        const std::optional<storage::Condition const>& _condition,
        std::function<void(Error::UniquePtr, std::vector<std::string>)> _callback) override;

    void asyncGetRow(std::string_view tableView, std::string_view keyView,
        std::function<void(Error::UniquePtr, std::optional<Entry>)> _callback) override;

    void asyncGetRows(std::string_view tableView,
        RANGES::any_view<std::string_view,
            RANGES::category::input | RANGES::category::random_access | RANGES::category::sized>
            keys,
        std::function<void(Error::UniquePtr, std::vector<std::optional<Entry>>)> _callback)
        override;

    void asyncSetRow(std::string_view tableView, std::string_view keyView, Entry entry,
        std::function<void(Error::UniquePtr)> callback) override;

    void parallelTraverse(bool onlyDirty, std::function<bool(const std::string_view& table,
                                              const std::string_view& key, const Entry& entry)>
                                              callback) const override;

    crypto::HashType hash(
        const bcos::crypto::Hash::Ptr& hashImpl, const ledger::Features& features) const override;

    void rollback(const Recoder& recoder) override;

    struct Data;
    class PageInfo
    {  // all methods is not thread safe
    public:
        auto operator<(const PageInfo& rhs) const -> bool
        {
            return m_data->pageKey < rhs.m_data->pageKey;
        }
        PageInfo() = default;
        ~PageInfo() = default;
        PageInfo(std::string _pageKey, uint16_t _count, uint16_t _size, Data* p)
          : m_data(std::make_shared<PageInfoData>(std::move(_pageKey), _count, _size)),
            m_pageData(p)
        {}
        PageInfo(PageInfo&&) = default;
        auto operator=(PageInfo&&) -> PageInfo& = default;
        PageInfo(const PageInfo& page) = default;
        auto operator=(const PageInfo& page) -> PageInfo& = default;

        void setPageKey(std::string key)
        {
            prepareMyData();
            m_data->pageKey = std::move(key);
        }
        [[nodiscard]] auto getPageKey() const -> std::string
        {
            if (m_data)
            {
                return m_data->pageKey;
            }
            return {};
        }
        void setCount(uint16_t _count)
        {
            prepareMyData();
            m_data->count = _count;
        }
        [[nodiscard]] auto getCount() const -> uint16_t
        {
            if (m_data)
            {
                return m_data->count;
            }
            return 0;
        }
        void setSize(uint16_t _size)
        {
            prepareMyData();
            m_data->size = _size;
        }
        [[nodiscard]] auto getSize() const -> uint16_t
        {
            if (m_data)
            {
                return m_data->size;
            }
            return 0;
        }

        [[nodiscard]] auto getPageData() const -> Data* { return m_pageData; }
        void setPageData(Data* data) { m_pageData = data; }

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
            PageInfoData() = default;
            PageInfoData(std::string _endKey, uint16_t _count, uint16_t _size)
              : pageKey(std::move(_endKey)), count(_count), size(_size)
            {}
            // startKey is not involved in comparison
            std::string pageKey;
            uint16_t count = 0;
            uint16_t size = 0;
        };
        std::shared_ptr<PageInfoData> m_data = std::make_shared<PageInfoData>();
        Data* m_pageData = nullptr;
        friend class boost::serialization::access;

        template <class Archive>
        void save(Archive& archive, const unsigned int version) const
        {
            std::ignore = version;
            archive & m_data->pageKey;
            archive & m_data->count;
            archive & m_data->size;
        }
        template <class Archive>
        void load(Archive& archive, const unsigned int version)
        {
            std::ignore = version;
            m_data = std::make_shared<PageInfoData>();
            archive & m_data->pageKey;
            archive & m_data->count;
            archive & m_data->size;
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
        TableMeta(const TableMeta& meta)
        {
            pages = std::make_unique<std::vector<PageInfo>>();
            pages->reserve(meta.pages->size());
            auto readLock = meta.rLock();
            *pages = *meta.pages;
        }
        TableMeta& operator=(const TableMeta& meta)
        {
            if (this != &meta)
            {
                pages = std::make_unique<std::vector<PageInfo>>();
                pages->reserve(meta.pages->size());
                auto readLock = meta.rLock();
                *pages = *meta.pages;
            }
            return *this;
        }
        TableMeta(TableMeta&& meta) noexcept : pages(std::move(meta.pages)) {}
        TableMeta& operator=(TableMeta&& meta) noexcept
        {
            if (this != &meta)
            {
                pages = std::move(meta.pages);
            }
            return *this;
        }
        auto lower_bound(const std::string_view& key) const
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
            ++getPageInfoCount;
            if (pages->empty())
            {  // if pages is empty
                return std::nullopt;
            }
            if (lastPageInfoIndex < pages->size())
            {
                auto* lastPageInfo = &pages->at(lastPageInfoIndex);
                if (lastPageInfo->getPageData())
                {
                    auto page = &std::get<0>(lastPageInfo->getPageData()->data);
                    if (!page->startKey().empty() && page->startKey() <= key &&
                        key <= page->endKey())
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

        auto getNextPageKeyNoLock(std::string_view key) -> std::optional<std::string>
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
            if (pageInfo.getPageKey().empty())
            {
                KeyPage_LOG(FATAL) << LOG_DESC("insert empty pageInfo");
                return;
            }
            auto it = lower_bound(pageInfo.getPageKey());
            auto newIt = pages->insert(it, std::move(pageInfo));
            KeyPage_LOG(DEBUG) << LOG_DESC("insert new pageInfo")
                               << LOG_KV("pageKey", toHex(newIt->getPageKey()))
                               << LOG_KV("valid", newIt->getCount())
                               << LOG_KV("size", newIt->getSize());
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
                    if (pageKey.empty())
                    {
                        p->setPageData(nullptr);
                    }
                }
                if (c_fileLogLevel <= TRACE)
                {
                    KeyPage_LOG(TRACE)
                        << LOG_DESC("updatePageInfo")
                        << LOG_KV("oldPageKey",
                               oldPageKey.has_value() ? toHex(oldPageKey.value()) : "not changed")
                        << LOG_KV("oldEndKey", toHex(oldEndKey))
                        << LOG_KV("newPageKey", toHex(pageKey)) << LOG_KV("count", count)
                        << LOG_KV("size", size);
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
                    KeyPage_LOG(FATAL) << LOG_DESC("updatePageInfo not found")
                                       << LOG_KV("oldEndKey", toHex(oldEndKey))
                                       << LOG_KV("pageKey", toHex(pageKey))
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
            for (auto it = pages->begin(); it != pages->end();)
            {
                it->setPageData(nullptr);
                if (it->getCount() == 0 || it->getPageKey().empty())
                {
                    KeyPage_LOG(DEBUG)
                        << LOG_DESC("TableMeta clean empty page") << LOG_KV("size", pages->size())
                        << LOG_KV("pageKey", toHex(it->getPageKey()));
                    it = pages->erase(it);
                }
                else
                {
                    ++it;
                }
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
        double hitRate() const { return hit / (double)getPageInfoCount; }
        uint64_t rowCount() const { return m_rows; }

        bool pageExist(const std::string_view& pageKey) const
        {
            auto it = lower_bound(pageKey);
            return it != pages->end() && it->getPageKey() == pageKey;
        }

    private:
        uint32_t getPageInfoCount = 0;
        uint32_t hit = 0;
        mutable uint64_t m_rows = 0;
        mutable std::shared_mutex mutex;
        std::unique_ptr<std::vector<PageInfo>> pages = nullptr;
        friend class boost::serialization::access;
        size_t lastPageInfoIndex = 0;
        template <class Archive>
        void save(Archive& ar, const unsigned int version) const
        {
            std::ignore = version;
            // auto len = (uint32_t)pages->size();
            // ar& len;
            // for (size_t i = 0; i < pages->size(); ++i)
            // {
            //     if (pages->at(i).getCount() == 0)
            //     {
            //         continue;
            //     }
            //     ar & pages->at(i);
            // }
            int invalid = 0;
            m_rows = 0;
            auto writeLock = lock();
            for (auto it = pages->begin(); it != pages->end();)
            {
                if (it->getCount() == 0 || it->getPageKey().empty())
                {
                    KeyPage_LOG(DEBUG)
                        << LOG_DESC("TableMeta empty page")
                        << LOG_KV("pageKey", toHex(it->getPageKey()))
                        << LOG_KV("count", it->getCount()) << LOG_KV("size", it->getSize());
                    it = pages->erase(it);
                    ++invalid;
                }
                else
                {
                    m_rows += it->getCount();
                    ++it;
                }
            }
            ar << *pages;
            KeyPage_LOG(DEBUG) << LOG_DESC("Serialize meta") << LOG_KV("valid", pages->size())
                               << LOG_KV("invalid", invalid);
        }
        template <class Archive>
        void load(Archive& ar, const unsigned int version)
        {
            std::ignore = version;
            // uint32_t s = 0;
            // ar& s;
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
        Page(const std::string_view& value, const std::string_view& pageKey)
        {
            if (value.empty())
            {
                return;
            }
            boost::iostreams::stream<boost::iostreams::array_source> inputStream(
                value.data(), value.size());
            boost::archive::binary_iarchive archive(inputStream, ARCHIVE_FLAG);
            archive >> *this;
            if (pageKey != entries.rbegin()->first)
            {
                KeyPage_LOG(INFO) << LOG_DESC("load page with invalid pageKey")
                                  << LOG_KV("pageKey", toHex(pageKey))
                                  << LOG_KV("validPageKey", toHex(entries.rbegin()->first))
                                  << LOG_KV("valid", m_validCount)
                                  << LOG_KV("count", entries.size());
                m_invalidPageKeys.insert(std::string(pageKey));
            }
        }
        Page(const Page& page)
          : entries(page.entries),
            m_size(page.m_size),
            m_validCount(page.m_validCount),
            m_invalidPageKeys(page.m_invalidPageKeys)
        {}
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
        Page(Page&& p) noexcept
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
                m_invalidPageKeys = std::move(p.m_invalidPageKeys);
            }
            return *this;
        }

        std::optional<Entry> getEntry(std::string_view key)
        {
            std::shared_lock lock(mutex);
            auto it = entries.find(key);
            if (it != entries.end())
            {
                // if (c_fileLogLevel <= bcos::LogLevel::TRACE)
                // {  // this log is only for debug, comment it when release
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
                // if (c_fileLogLevel <= bcos::LogLevel::TRACE)
                // {  // this log is only for debug, comment it when release
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
        {  // do not exist entry optimization: insert a empty entry to cache, then
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
                        m_size += it->first.size();
                    }
                }
                else
                {
                    if (it->second.status() != Entry::Status::DELETED)
                    {
                        --m_validCount;
                        pageInfoChanged = true;
                        m_size -= entry.size();
                        m_size -= it->first.size();
                    }
                }
                if (m_invalidPageKeys.size() == 1)
                {
                    pageInfoChanged = true;
                }
                ret = std::move(it->second);
                it->second = std::move(entry);
                // if (c_fileLogLevel <= bcos::LogLevel::TRACE)
                // {  // this log is only for debug, comment it when release
                //     KeyPage_LOG(TRACE)
                //         << LOG_DESC("setEntry update")
                //         << LOG_KV("pageKey", toHex(entries.rbegin()->first))
                //         << LOG_KV("valid", m_validCount) << LOG_KV("count", entries.size())
                //         << LOG_KV("key", toHex(key)) << LOG_KV("status",
                //         (int)it->second.status())
                //         << LOG_KV("pageInfoChanged", pageInfoChanged);
                // }
            }
            else
            {
                pageInfoChanged = true;

                if (entry.status() != Entry::Status::DELETED)
                {
                    ++m_validCount;
                    m_size += key.size();
                }
                else
                {
                    m_size -= entry.size();
                }
                if (entries.empty() || key > entries.rbegin()->first)
                {
                    if (!entries.empty())
                    {  // means key > entries.rbegin()->first is true
                        m_invalidPageKeys.insert(entries.rbegin()->first);
                    }
                    auto it = m_invalidPageKeys.find(std::string(key));
                    if (it != m_invalidPageKeys.end())
                    {
                        m_invalidPageKeys.erase(it);
                        KeyPage_LOG(DEBUG) << LOG_DESC("invalid pageKey become valid")
                                           << LOG_KV("pageKey", toHex(key));
                    }
                }
                entries.insert(it, std::make_pair(std::string(key), std::move(entry)));
                // if (c_fileLogLevel <= bcos::LogLevel::TRACE)
                // {  // this log is only for debug, comment it when release
                //     KeyPage_LOG(TRACE) << LOG_DESC("setEntry insert")
                //                        << LOG_KV("pageKey", toHex(entries.rbegin()->first))
                //                        << LOG_KV("key", toHex(key)) << LOG_KV("valid",
                //                        m_validCount)
                //                        << LOG_KV("count", entries.size())
                //                        << LOG_KV("pageInfoChanged", pageInfoChanged);
                // }
            }
            return std::make_tuple(std::move(ret), pageInfoChanged);
        }
        auto size() const -> size_t
        {
            std::shared_lock lock(mutex);
            return m_size;
        }
        auto validCount() const -> size_t
        {
            std::shared_lock lock(mutex);
            return m_validCount;
        }
        auto count() const -> size_t
        {
            std::shared_lock lock(mutex);
            return entries.size();
        }
        auto invalidKeySet() const -> const std::set<std::string>&
        {
            std::shared_lock lock(mutex);
            return m_invalidPageKeys;
        }
        auto invalidKeyCount() const -> size_t { return m_invalidPageKeys.size(); }
        auto startKey() const -> std::string
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
                if (!m_invalidPageKeys.empty() && iter->first >= *m_invalidPageKeys.begin())
                {
                    page.m_invalidPageKeys.insert(
                        m_invalidPageKeys.extract(m_invalidPageKeys.begin()));
                }
                auto ret = page.entries.insert(entries.extract(iter));
                assert(ret.inserted);
                iter = entries.begin();
                if (m_size - iter->second.size() < threshold || entries.size() == 1)
                {
                    break;
                }
            }
            // if new pageKey has been pageKey, remove it from m_invalidPageKeys
            auto it = m_invalidPageKeys.find(page.entries.rbegin()->first);
            if (it != m_invalidPageKeys.end())
            {
                m_invalidPageKeys.erase(it);
                KeyPage_LOG(DEBUG) << LOG_DESC("invalid pageKey become valid because of split")
                                   << LOG_KV("pageKey", toHex(page.entries.rbegin()->first));
            }
            it = page.m_invalidPageKeys.find(page.entries.rbegin()->first);
            if (it != page.m_invalidPageKeys.end())
            {
                page.m_invalidPageKeys.erase(it);
                KeyPage_LOG(DEBUG) << LOG_DESC("invalid pageKey become valid because of split")
                                   << LOG_KV("pageKey", toHex(page.entries.rbegin()->first));
            }
            return page;
        }

        void merge(Page& p)
        {
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
                // Merges two sorted lists into one
                m_invalidPageKeys.merge(p.m_invalidPageKeys);
            }
            else
            {
                KeyPage_LOG(WARNING)
                    << LOG_DESC("merge self") << LOG_KV("startKey", toHex(entries.begin()->first))
                    << LOG_KV("endKey", toHex(entries.rbegin()->first))
                    << LOG_KV("valid", m_validCount) << LOG_KV("count", entries.size());
            }
        }
        void clean(const std::string_view& pageKey)
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
            if (!entries.empty() && pageKey != entries.rbegin()->first)
            {
                KeyPage_LOG(DEBUG) << LOG_DESC("import page with invalid pageKey")
                                   << LOG_KV("pageKey", toHex(pageKey))
                                   << LOG_KV("validPageKey", toHex(entries.rbegin()->first))
                                   << LOG_KV("count", entries.size());
                m_invalidPageKeys.insert(std::string(pageKey));
            }
            if (entries.empty())
            {
                KeyPage_LOG(DEBUG)
                    << LOG_DESC("import empty page") << LOG_KV("pageKey", toHex(pageKey))
                    << LOG_KV("count", entries.size());
            }
        }
        auto hash(const std::string& table, const bcos::crypto::Hash::Ptr& hashImpl,
            uint32_t blockVersion) const -> crypto::HashType
        {
            bcos::crypto::HashType pageHash(0);
            auto hash = hashImpl->hash(table);
            // std::shared_lock lock(mutex);
            for (const auto& entry : entries)
            {
                if (entry.second.dirty())
                {
                    bcos::crypto::HashType entryHash(0);
                    if (blockVersion >= (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION)
                    {
                        entryHash = entry.second.hash(table, entry.first, *hashImpl, blockVersion);
                    }
                    else
                    {  // 3.0.0
                        entryHash = hash ^ hashImpl->hash(entry.first) ^
                                    entry.second.hash(table, entry.first, *hashImpl, blockVersion);
                    }
                    // if (c_fileLogLevel <= TRACE)
                    // {
                    //     KeyPage_LOG(TRACE)
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
                    if (c_fileLogLevel <= bcos::LogLevel::TRACE)
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
                    it->second = *change.entry;
                    m_size += it->second.size();
                }
                else
                {  // delete, should not happen?
                    if (c_fileLogLevel <= bcos::LogLevel::TRACE)
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
                    if (c_fileLogLevel <= bcos::LogLevel::TRACE)
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
                        m_invalidPageKeys.erase(std::prev(m_invalidPageKeys.end()));
                    }
                }
                else
                {  // delete a key not exist
                    KeyPage_LOG(DEBUG)
                        << "Revert invalid delete: " << change.table << " | " << toHex(change.key);
                    auto message =
                        fmt::format("Not found rollback entry: {}:{}", change.table, change.key);
                    BOOST_THROW_EXCEPTION(BCOS_ERROR(StorageError::UnknownError, message));
                }
            }
        }
        auto lock() -> std::unique_lock<std::shared_mutex> { return std::unique_lock(mutex); }
        auto rLock() -> std::shared_lock<std::shared_mutex> { return std::shared_lock(mutex); }
        void setTableMeta(TableMeta* _meta) { m_meta = _meta; }
        TableMeta* myTableMeta() { return m_meta; }

    private:
        //   PageInfo* pageInfo;
        mutable std::shared_mutex mutex;
        std::map<std::string, Entry, std::less<>> entries;
        uint32_t m_size = 0;        // page real size
        uint32_t m_validCount = 0;  // valid entry count
        friend class boost::serialization::access;
        // if startKey changed the old startKey need keep to delete old page
        std::set<std::string> m_invalidPageKeys;
        TableMeta* m_meta = nullptr;

        template <class Archive>
        void save(Archive& ar, const unsigned int version) const
        {
            std::ignore = version;
            ar&(uint32_t)m_validCount;
            [[maybe_unused]] size_t count = 0;
            for (const auto& i : entries)
            {
                if (i.second.status() == Entry::Status::DELETED)
                {  // skip deleted entry
                    continue;
                }
                ++count;
                ar & i.first;
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
            ar & count;
            m_validCount = count;
            auto iter = entries.begin();
            for (size_t i = 0; i < m_validCount; ++i)
            {
                std::string key;
                ar & key;
                uint32_t len = 0;
                ar & len;
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
        Data() = default;
        ~Data() = default;
        Data(std::string _table, std::string _key, Entry _entry, Type _type)
          : table(std::move(_table)), key(std::move(_key)), type(_type), entry(std::move(_entry))
        {
            if (type == Type::TableMeta)
            {
                auto meta = KeyPageStorage::TableMeta(entry.get());
                if (c_fileLogLevel <= TRACE)
                {
                    KeyPage_LOG(TRACE) << LOG_DESC("Data TableMeta") << LOG_KV("table", table)
                                       << LOG_KV("len", entry.size()) << LOG_KV("size", meta.size())
                                       << LOG_KV("meta", meta);
                }
                data = std::move(meta);
            }
            else if (type == Type::Page)
            {
                auto page = KeyPageStorage::Page(entry.get(), key);
                if (c_fileLogLevel <= TRACE)
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
        KeyPageStorage::Page* getPage()
        {
            if (type == Type::Page)
            {
                return &std::get<0>(data);
            }
            return nullptr;
        }
        KeyPageStorage::TableMeta* getTableMeta()
        {
            if (type == Type::TableMeta)
            {
                return &std::get<1>(data);
            }
            return nullptr;
        }
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
        Bucket() = default;
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
            // copy data also need clean
            auto data = std::make_shared<Data>(*it->second);
            return std::make_optional(std::move(data));
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
                               << LOG_KV("message", error->errorMessage());
            return std::nullopt;
        }
        if (c_fileLogLevel <= TRACE)
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
    virtual std::pair<size_t, Error::Ptr> count(const std::string_view& table) override;

private:
    auto getPrev() -> std::shared_ptr<StorageInterface>
    {
        std::shared_lock<std::shared_mutex> lock(m_prevMutex);
        return m_prev;
    }
    auto getBucketIndex(std::string_view table, std::string_view key) const -> size_t
    {
        auto hash = std::hash<std::string_view>{}(table);
        std::ignore = key;
        // the table must in one bucket
        // boost::hash_combine(hash, std::hash<std::string_view>{}(key));
        return hash % m_buckets.size();
    }

    auto changePageKey(std::string table, const std::string& oldPageKey,
        const std::string& newPageKey, bool isRevert = false) -> Data*
    {
        if (newPageKey.empty() && !isRevert)
        {
            KeyPage_LOG(FATAL) << LOG_DESC("changePageKey to empty") << LOG_KV("table", table)
                               << LOG_KV("oldPageKey", toHex(oldPageKey))
                               << LOG_KV("newPageKey", toHex(newPageKey));
            return nullptr;
        }

        auto [bucket, lock] = getMutBucket(table, oldPageKey);
        boost::ignore_unused(lock);
        auto node = bucket->container.extract(std::make_pair(table, oldPageKey));
        auto* page = &std::get<0>(node.mapped()->data);
        KeyPage_LOG(DEBUG) << LOG_DESC("changePageKey") << LOG_KV("table", table)
                           << LOG_KV("oldPageKey", toHex(oldPageKey))
                           << LOG_KV("newPageKey", toHex(newPageKey))
                           << LOG_KV("validCount", page->validCount());
        node.key().second = newPageKey;
        node.mapped()->key = newPageKey;
        if (newPageKey.empty())
        {
            return nullptr;
        }
        auto it = bucket->container.find(std::make_pair(table, newPageKey));
        if (it != bucket->container.end())
        {  // erase old page to update data
            bucket->container.erase(it);
        }
        auto ret = bucket->container.insert(std::move(node));
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
    uint32_t m_blockVersion = 0;
    size_t m_pageSize = 8 * 1024;
    size_t m_splitSize;
    size_t m_mergeSize;
    std::atomic_uint64_t m_readLength{0};
    std::atomic_uint64_t m_writeLength{0};
    mutable std::atomic_uint64_t m_size{0};
    std::vector<Bucket> m_buckets;
    std::shared_ptr<const std::set<std::string, std::less<>>> m_ignoreTables;
    bool m_ignoreNotExist = false;
    bool m_setRowWithDirtyFlag = false;
};

}  // namespace bcos::storage
