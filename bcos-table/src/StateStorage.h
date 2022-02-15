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

#include "bcos-framework/interfaces/storage/StorageInterface.h"
#include "bcos-framework/interfaces/storage/Table.h"
#include "bcos-utilities/Error.h"
#include "tbb/enumerable_thread_specific.h"
#include <boost/core/ignore_unused.hpp>
#include <boost/format.hpp>
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
#include <type_traits>

namespace bcos::storage
{
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

template <bool enableLRU = false>
class BaseStorage : public virtual storage::TraverseStorageInterface,
                    public virtual storage::MergeableStorageInterface
{
private:
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

public:
    using Ptr = std::shared_ptr<BaseStorage<enableLRU>>;

    explicit BaseStorage(std::shared_ptr<StorageInterface> prev)
      : storage::TraverseStorageInterface(),
        m_prev(std::move(prev)),
        m_buckets(std::thread::hardware_concurrency())
    {}

    BaseStorage(const BaseStorage&) = delete;
    BaseStorage& operator=(const BaseStorage&) = delete;

    BaseStorage(BaseStorage&&) = delete;
    BaseStorage& operator=(BaseStorage&&) = delete;

    virtual ~BaseStorage() { m_recoder.clear(); }

    void asyncGetPrimaryKeys(std::string_view table,
        const std::optional<storage::Condition const>& _condition,
        std::function<void(Error::UniquePtr, std::vector<std::string>)> _callback) override
    {
        std::map<std::string_view, storage::Entry::Status> localKeys;

        if (m_enableTraverse)
        {
#pragma omp parallel for
            for (size_t i = 0; i < m_buckets.size(); ++i)
            {
                auto& bucket = m_buckets[i];
                std::unique_lock<std::mutex> lock(bucket.mutex);

                decltype(localKeys) bucketKeys;
                for (auto& it : bucket.container)
                {
                    if (it.table == table && (!_condition || _condition->isValid(it.key)))
                    {
                        bucketKeys.emplace(it.key, it.entry.status());
                    }
                }

#pragma omp critical
                localKeys.merge(std::move(bucketKeys));
            }
        }

        auto prev = getPrev();
        if (!prev)
        {
            std::vector<std::string> resultKeys;
            for (auto& localIt : localKeys)
            {
                if (localIt.second == Entry::NORMAL)
                {
                    resultKeys.push_back(std::string(localIt.first));
                }
            }

            _callback(nullptr, std::move(resultKeys));
            return;
        }

        prev->asyncGetPrimaryKeys(table, _condition,
            [localKeys = std::move(localKeys), callback = std::move(_callback)](
                auto&& error, auto&& remoteKeys) mutable {
                if (error)
                {
                    callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(StorageError::ReadError,
                                 "Get primary keys from prev failed!", *error),
                        std::vector<std::string>());
                    return;
                }

                for (auto it = remoteKeys.begin(); it != remoteKeys.end();)
                {
                    bool deleted = false;

                    auto localIt = localKeys.find(*it);
                    if (localIt != localKeys.end())
                    {
                        if (localIt->second != Entry::NORMAL)
                        {
                            it = remoteKeys.erase(it);
                            deleted = true;
                        }

                        localKeys.erase(localIt);
                    }

                    if (!deleted)
                    {
                        ++it;
                    }
                }

                for (auto& localIt : localKeys)
                {
                    if (localIt.second == Entry::NORMAL)
                    {
                        remoteKeys.push_back(std::string(localIt.first));
                    }
                }

                callback(nullptr, std::move(remoteKeys));
            });
    }

    void asyncGetRow(std::string_view tableView, std::string_view keyView,
        std::function<void(Error::UniquePtr, std::optional<Entry>)> _callback) override
    {
        auto [bucket, lock] = getBucket(tableView, keyView);
        boost::ignore_unused(lock);

        auto it = bucket->container.template get<0>().find(std::make_tuple(tableView, keyView));
        if (it != bucket->container.template get<0>().end())
        {
            auto& entry = it->entry;

            if (entry.status() != Entry::NORMAL)
            {
                lock.unlock();

                STORAGE_REPORT_GET(tableView, keyView, std::nullopt, "DELETED");
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

                STORAGE_REPORT_GET(tableView, keyView, optionalEntry, "FOUND");
                _callback(nullptr, std::move(optionalEntry));
            }
            return;
        }
        else
        {
            STORAGE_REPORT_GET(tableView, keyView, std::nullopt, "NO ENTRY");
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
                        STORAGE_REPORT_GET(table, key, entry, "PREV FOUND");

                        _callback(nullptr,
                            std::make_optional(importExistingEntry(table, key, std::move(*entry))));
                    }
                    else
                    {
                        STORAGE_REPORT_GET(table, key, std::nullopt, "PREV NOT FOUND");

                        _callback(nullptr, std::nullopt);
                    }
                });
        }
        else
        {
            _callback(nullptr, std::nullopt);
        }
    }

    void asyncGetRows(std::string_view tableView,
        const std::variant<const gsl::span<std::string_view const>,
            const gsl::span<std::string const>>& _keys,
        std::function<void(Error::UniquePtr, std::vector<std::optional<Entry>>)> _callback) override
    {
        std::visit(
            [this, &tableView, &_callback](auto&& _keys) {
                std::vector<std::optional<Entry>> results(_keys.size());
                auto missinges = std::tuple<std::vector<std::string_view>,
                    std::vector<std::tuple<std::string, size_t>>>();

                std::atomic_long existsCount = 0;

#pragma omp parallel for
                for (gsl::index i = 0; i < _keys.size(); ++i)
                {
                    auto [bucket, lock] = getBucket(tableView, _keys[i]);
                    boost::ignore_unused(lock);

                    auto it = bucket->container.find(
                        std::make_tuple(tableView, std::string_view(_keys[i])));
                    if (it != bucket->container.end())
                    {
                        auto& entry = it->entry;
                        if (entry.status() == Entry::NORMAL)
                        {
                            results[i].emplace(entry);

                            if constexpr (enableLRU)
                            {
                                updateMRUAndCheck(*bucket, it);
                            }
                        }
                        else
                        {
                            results[i] = std::nullopt;
                        }
                        ++existsCount;
                    }
                    else
                    {
#pragma omp critical
                        {
                            std::get<1>(missinges).emplace_back(std::string(_keys[i]), i);
                            std::get<0>(missinges).emplace_back(_keys[i]);
                        }
                    }
                }

                auto prev = getPrev();
                if (existsCount < _keys.size() && prev)
                {
                    prev->asyncGetRows(tableView, std::get<0>(missinges),
                        [this, table = std::string(tableView), callback = std::move(_callback),
                            missingIndexes = std::move(std::get<1>(missinges)),
                            results = std::move(results)](
                            auto&& error, std::vector<std::optional<Entry>>&& entries) mutable {
                            if (error)
                            {
                                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(StorageError::ReadError,
                                             "async get perv rows failed!", *error),
                                    std::vector<std::optional<Entry>>());
                                return;
                            }

#pragma omp parallel for
                            for (size_t i = 0; i < entries.size(); ++i)
                            {
                                auto& entry = entries[i];

                                if (entry)
                                {
                                    results[std::get<1>(missingIndexes[i])].emplace(
                                        importExistingEntry(table,
                                            std::move(std::get<0>(missingIndexes[i])),
                                            std::move(*entry)));
                                }
                            }

                            callback(nullptr, std::move(results));
                        });
                }
                else
                {
                    _callback(nullptr, std::move(results));
                }
            },
            _keys);
    }

    void asyncSetRow(std::string_view tableView, std::string_view keyView, Entry entry,
        std::function<void(Error::UniquePtr)> callback) override
    {
        if (m_readOnly)
        {
            callback(BCOS_ERROR_UNIQUE_PTR(
                StorageError::ReadOnly, "Try to operate a read-only storage"));
            return;
        }

        ssize_t updatedCapacity = entry.size();
        std::optional<Entry> entryOld;

        auto [bucket, lock] = getBucket(tableView, keyView);
        boost::ignore_unused(lock);

        auto it = bucket->container.find(std::make_tuple(tableView, keyView));
        if (it != bucket->container.end())
        {
            auto& existsEntry = it->entry;
            entryOld.emplace(std::move(existsEntry));

            updatedCapacity -= entryOld->size();

            STORAGE_REPORT_SET(tableView, keyView, entry, "UPDATE");
            bucket->container.modify(it, [&entry](Data& data) { data.entry = std::move(entry); });

            if constexpr (enableLRU)
            {
                updateMRUAndCheck(*bucket, it);
            }
        }
        else
        {
            bucket->container.emplace(
                Data{std::string(tableView), std::string(keyView), std::move(entry)});

            STORAGE_REPORT_SET(tableView, keyView, std::nullopt, "INSERT");
        }

        if (m_recoder.local())
        {
            m_recoder.local()->log(
                Recoder::Change(std::string(tableView), std::string(keyView), std::move(entryOld)));
        }

        bucket->capacity += updatedCapacity;

        lock.unlock();
        callback(nullptr);
    }

    void parallelTraverse(bool onlyDirty, std::function<bool(const std::string_view& table,
                                              const std::string_view& key, const Entry& entry)>
                                              callback) const override
    {
#pragma omp parallel for
        for (size_t i = 0; i < m_buckets.size(); ++i)
        {
            auto& bucket = m_buckets[i];

            for (auto& it : bucket.container)
            {
                auto& entry = it.entry;
                if (!onlyDirty || entry.dirty())
                {
                    callback(it.table, it.key, entry);
                }
            }
        }
    }

    void merge(bool onlyDirty, const TraverseStorageInterface& source) override
    {
        if (&source == this)
        {
            STORAGE_LOG(ERROR) << "Can't merge from self!";
            BOOST_THROW_EXCEPTION(BCOS_ERROR(-1, "Can't merge from self!"));
        }

        std::atomic_size_t count = 0;
        source.parallelTraverse(
            onlyDirty, [this, &count](const std::string_view& table, const std::string_view& key,
                           const storage::Entry& entry) {
                asyncSetRow(table, key, entry, [](Error::UniquePtr) {});
                ++count;
                return true;
            });

        STORAGE_LOG(INFO) << "Successful merged " << count << " records";
    }

    std::optional<Table> openTable(const std::string_view& tableView)
    {
        std::promise<std::tuple<Error::UniquePtr, std::optional<Table>>> openPromise;
        asyncOpenTable(tableView, [&](auto&& error, auto&& table) {
            openPromise.set_value({std::move(error), std::move(table)});
        });

        auto [error, table] = openPromise.get_future().get();
        if (error)
        {
            BOOST_THROW_EXCEPTION(*error);
        }

        return table;
    }

    std::optional<Table> createTable(std::string _tableName, std::string _valueFields)
    {
        std::promise<std::tuple<Error::UniquePtr, std::optional<Table>>> createPromise;
        asyncCreateTable(
            _tableName, _valueFields, [&](Error::UniquePtr&& error, std::optional<Table>&& table) {
                createPromise.set_value({std::move(error), std::move(table)});
            });
        auto [error, table] = createPromise.get_future().get();
        if (error)
        {
            BOOST_THROW_EXCEPTION(*error);
        }

        return table;
    }

    crypto::HashType hash(const bcos::crypto::Hash::Ptr& hashImpl) const
    {
        bcos::crypto::HashType totalHash;

#pragma omp parallel for
        for (size_t i = 0; i < m_buckets.size(); ++i)
        {
            auto& bucket = m_buckets[i];
            bcos::crypto::HashType bucketHash;

            for (auto& it : bucket.container)
            {
                auto& entry = it.entry;
                if (entry.dirty())
                {
                    auto hash = hashImpl->hash(it.table);
                    hash ^= hashImpl->hash(it.key);

                    if (entry.status() != Entry::DELETED)
                    {
                        auto value = entry.getField(0);
                        bcos::bytesConstRef ref((const bcos::byte*)value.data(), value.size());
                        auto entryHash = hashImpl->hash(ref);
                        if (c_fileLogLevel >= TRACE)
                        {
                            STORAGE_LOG(TRACE)
                                << "Calc hash, dirty entry: " << it.table << " | " << toHex(it.key)
                                << " | " << toHex(value) << LOG_KV("hash", entryHash.abridged());
                        }
                        hash ^= entryHash;
                    }
                    else
                    {
                        auto entryHash = bcos::crypto::HashType(0x1);
                        if (c_fileLogLevel >= TRACE)
                        {
                            STORAGE_LOG(TRACE)
                                << "Calc hash, deleted entry: " << it.table << " | "
                                << toHex(toHex(it.key)) << LOG_KV("hash", entryHash.abridged());
                        }
                        hash ^= entryHash;
                    }
                    bucketHash ^= hash;
                }
            }
#pragma omp critical
            totalHash ^= bucketHash;
        }

        return totalHash;
    }

    void setPrev(std::shared_ptr<StorageInterface> prev)
    {
        std::unique_lock<std::shared_mutex> lock(m_prevMutex);
        m_prev = std::move(prev);
    }

    typename Recoder::Ptr newRecoder() { return std::make_shared<Recoder>(); }
    void setRecoder(typename Recoder::Ptr recoder) { m_recoder.local().swap(recoder); }
    void rollback(const Recoder& recoder)
    {
        if (m_readOnly)
        {
            return;
        }

        for (auto& change : recoder)
        {
            ssize_t updateCapacity = 0;
            auto [bucket, lock] = getBucket(change.table, change.key);
            boost::ignore_unused(lock);

            auto it = bucket->container.find(
                std::make_tuple(std::string_view(change.table), std::string_view(change.key)));
            if (change.entry)
            {
                if (it != bucket->container.end())
                {
                    if (c_fileLogLevel >= bcos::LogLevel::TRACE)
                    {
                        STORAGE_LOG(TRACE)
                            << "Revert exists: " << change.table << " | " << toHex(change.key)
                            << " | " << toHex(change.entry->get());
                    }

                    updateCapacity = change.entry->size() - it->entry.size();

                    auto& rollbackEntry = change.entry;
                    bucket->container.modify(it,
                        [&rollbackEntry](Data& data) { data.entry = std::move(*rollbackEntry); });
                }
                else
                {
                    if (c_fileLogLevel >= bcos::LogLevel::TRACE)
                    {
                        STORAGE_LOG(TRACE)
                            << "Revert deleted: " << change.table << " | " << toHex(change.key)
                            << " | " << toHex(change.entry->get());
                    }
                    updateCapacity = change.entry->size();
                    bucket->container.emplace(
                        Data{change.table, change.key, std::move(*(change.entry))});
                }
            }
            else
            {  // nullopt means the key is not exist in m_cache
                if (it != bucket->container.end())
                {
                    if (c_fileLogLevel >= bcos::LogLevel::TRACE)
                    {
                        STORAGE_LOG(TRACE)
                            << "Revert insert: " << change.table << " | " << toHex(change.key);
                    }

                    updateCapacity = 0 - it->entry.size();
                    bucket->container.erase(it);
                }
                else
                {
                    auto message = (boost::format("Not found rollback entry: %s:%s") %
                                    change.table % change.key)
                                       .str();

                    BOOST_THROW_EXCEPTION(BCOS_ERROR(StorageError::UnknownError, message));
                }
            }

            bucket->capacity += updateCapacity;
        }
    }

    void setEnableTraverse(bool enableTraverse) { m_enableTraverse = enableTraverse; }
    void setReadOnly(bool readOnly) { m_readOnly = readOnly; }
    void setMaxCapacity(ssize_t capacity) { m_maxCapacity = capacity; }

private:
    Entry importExistingEntry(std::string_view table, std::string_view key, Entry entry)
    {
        if (m_readOnly)
        {
            return entry;
        }

        entry.setDirty(false);

        auto updateCapacity = entry.size();

        auto [bucket, lock] = getBucket(table, key);
        boost::ignore_unused(lock);
        auto it = bucket->container.find(std::make_tuple(table, key));

        if (it == bucket->container.end())
        {
            STORAGE_REPORT_SET(
                std::get<0>(entryIt->first), key, std::make_optional(entryIt->second), "IMPORT");
            it = bucket->container
                     .emplace(Data{std::string(table), std::string(key), std::move(entry)})
                     .first;

            bucket->capacity += updateCapacity;
        }
        else
        {
            STORAGE_REPORT_SET(
                std::get<0>(entryIt->first), key, entryIt->second, "IMPORT EXISTS FAILED");

            STORAGE_LOG(WARNING) << "Fail import existsing entry, " << table << " | " << toHex(key);
        }

        return it->entry;
    }

    std::shared_ptr<StorageInterface> getPrev()
    {
        std::shared_lock<std::shared_mutex> lock(m_prevMutex);
        auto prev = m_prev;
        return prev;
    }

    tbb::enumerable_thread_specific<typename Recoder::Ptr> m_recoder;

    std::shared_ptr<StorageInterface> m_prev;
    std::shared_mutex m_prevMutex;

    bool m_enableTraverse = false;
    bool m_readOnly = false;

    ssize_t m_maxCapacity = 32 * 1024 * 1024;

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
        boost::multi_index::indexed_by<boost::multi_index::hashed_unique<boost::multi_index::
                const_mem_fun<Data, std::tuple<std::string_view, std::string_view>, &Data::view>>>>;
    using LRUHashContainer = boost::multi_index_container<Data,
        boost::multi_index::indexed_by<
            boost::multi_index::hashed_unique<boost::multi_index::const_mem_fun<Data,
                std::tuple<std::string_view, std::string_view>, &Data::view>>,
            boost::multi_index::sequenced<>>>;
    using Container = std::conditional_t<enableLRU, LRUHashContainer, HashContainer>;

    struct Bucket
    {
        Container container;
        std::mutex mutex;
        ssize_t capacity = 0;
    };
    std::vector<Bucket> m_buckets;

    std::tuple<Bucket*, std::unique_lock<std::mutex>> getBucket(
        std::string_view table, std::string_view key)
    {
        auto hash = std::hash<std::string_view>{}(table);
        boost::hash_combine(hash, std::hash<std::string_view>{}(key));
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

using StateStorage = BaseStorage<false>;
using LRUStateStorage = BaseStorage<true>;

}  // namespace bcos::storage
