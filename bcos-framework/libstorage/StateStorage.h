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
 * @brief interface of Table
 * @file Table.h
 * @author: xingqiangbai
 * @date: 2021-04-07
 * @brief interface of Table
 * @file StateStorage.h
 * @author: ancelmo
 * @date: 2021-09-01
 */
#pragma once

#include "../interfaces/storage/StorageInterface.h"
#include "../interfaces/storage/Table.h"
#include "../libutilities/Error.h"
#include "tbb/enumerable_thread_specific.h"
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/key.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/property_map/property_map.hpp>
#include <boost/throw_exception.hpp>
#include <future>
#include <memory>
#include <optional>
#include <shared_mutex>

namespace bcos::storage
{
class StateStorage : public virtual storage::TraverseStorageInterface
{
public:
    using Ptr = std::shared_ptr<StateStorage>;

    explicit StateStorage(std::shared_ptr<StorageInterface> prev)
      : storage::TraverseStorageInterface(),
        m_prev(std::move(prev)),
        m_buckets(std::thread::hardware_concurrency())
    {}

    StateStorage(const StateStorage&) = delete;
    StateStorage& operator=(const StateStorage&) = delete;

    StateStorage(StateStorage&&) = delete;
    StateStorage& operator=(StateStorage&&) = delete;

    virtual ~StateStorage() { m_recoder.clear(); }

    void asyncGetPrimaryKeys(std::string_view table,
        const std::optional<storage::Condition const>& _condition,
        std::function<void(Error::UniquePtr, std::vector<std::string>)> _callback) override;

    void asyncGetRow(std::string_view table, std::string_view _key,
        std::function<void(Error::UniquePtr, std::optional<Entry>)> _callback) override;

    void asyncGetRows(std::string_view table,
        const std::variant<const gsl::span<std::string_view const>,
            const gsl::span<std::string const>>& _keys,
        std::function<void(Error::UniquePtr, std::vector<std::optional<Entry>>)> _callback)
        override;

    void asyncSetRow(std::string_view table, std::string_view key, Entry entry,
        std::function<void(Error::UniquePtr)> callback) override;

    void parallelTraverse(bool onlyDirty, std::function<bool(const std::string_view& table,
                                              const std::string_view& key, const Entry& entry)>
                                              callback) const override;

    std::optional<Table> openTable(const std::string_view& table);

    std::optional<Table> createTable(std::string _tableName, std::string _valueFields);

    crypto::HashType hash(const bcos::crypto::Hash::Ptr& hashImpl) const;

    void setPrev(std::shared_ptr<StorageInterface> prev)
    {
        std::unique_lock<std::shared_mutex> lock(m_prevMutex);
        m_prev = std::move(prev);
    }

    class Recoder
    {
    public:
        using Ptr = std::shared_ptr<Recoder>;
        using ConstPtr = std::shared_ptr<Recoder>;

        struct Change
        {
            Change(std::string _table, std::string _key, std::optional<Entry> _entry)
              : table(std::move(_table)), key(std::move(_key)), entry(std::move(_entry))
            {}
            Change(const Change&) = delete;
            Change& operator=(const Change&) = delete;
            Change(Change&&) noexcept = default;
            Change& operator=(Change&&) noexcept = default;

            std::string table;
            std::string key;
            std::optional<Entry> entry;
        };

        void log(Change&& change) { m_changes.emplace_front(std::move(change)); }
        auto begin() const { return m_changes.cbegin(); }
        auto end() const { return m_changes.cend(); }
        void clear() { m_changes.clear(); }

    private:
        std::list<Change> m_changes;
    };

    Recoder::Ptr newRecoder() { return std::make_shared<Recoder>(); }
    void setRecoder(Recoder::Ptr recoder) { m_recoder.local().swap(recoder); }
    void rollback(const Recoder& recoder);

    void setEnableTraverse(bool enableTraverse) { m_enableTraverse = enableTraverse; }
    void setReadOnly(bool readOnly) { m_readOnly = readOnly; }

private:
    Entry importExistingEntry(std::string_view table, std::string_view key, Entry entry);

    std::shared_ptr<StorageInterface> getPrev()
    {
        std::shared_lock<std::shared_mutex> lock(m_prevMutex);
        auto prev = m_prev;
        return prev;
    }

    tbb::enumerable_thread_specific<Recoder::Ptr> m_recoder;

    std::shared_ptr<StorageInterface> m_prev;
    std::shared_mutex m_prevMutex;

    bool m_enableTraverse = false;
    bool m_readOnly = false;

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

    using Container = boost::multi_index_container<Data,
        boost::multi_index::indexed_by<
            boost::multi_index::hashed_unique<boost::multi_index::const_mem_fun<Data,
                std::tuple<std::string_view, std::string_view>, &Data::view>>,
            boost::multi_index::sequenced<>>>;

    struct Bucket
    {
        Container container;
        std::mutex mutex;
        size_t capacity = 0;
    };
    std::vector<Bucket> m_buckets;

    std::tuple<Bucket*, std::unique_lock<std::mutex>> getBucket(
        std::string_view table, std::string_view key);

#define STORAGE_REPORT_GET(table, key, entry, desc) \
    if (c_fileLogLevel >= bcos::LogLevel::TRACE)    \
    {                                               \
    }                                               \
    // log("GET", (table), (key), (entry), (desc))

#define STORAGE_REPORT_SET(table, key, entry, desc) \
    if (c_fileLogLevel >= bcos::LogLevel::TRACE)    \
    {                                               \
    }                                               \
    // log("SET", (table), (key), (entry), (desc))

    // for debug
    void log(const std::string_view& op, const std::string_view& table, const std::string_view& key,
        const std::optional<Entry>& entry, const std::string_view& desc = "")
    {
        if (!m_readOnly)
        {
            if (entry)
            {
                STORAGE_LOG(TRACE)
                    << op << "|" << table << "|" << toHex(key) << "|[" << toHex(entry->getField(0))
                    << "]|" << (int32_t)entry->status() << "|" << desc;
            }
            else
            {
                STORAGE_LOG(TRACE) << op << "|" << table << "|" << toHex(key) << "|"
                                   << "[]"
                                   << "|"
                                   << "NO ENTRY"
                                   << "|" << desc;
            }
        }
    }
};


}  // namespace bcos::storage
