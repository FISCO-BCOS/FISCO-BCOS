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

#include "StateStorageInterface.h"
#include <bcos-crypto/interfaces/crypto/Hash.h>
#include <bcos-utilities/Error.h>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <boost/core/ignore_unused.hpp>
#include <boost/format.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/key.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>
#include <mutex>

namespace bcos::storage
{
template <bool enableLRU = false>
class BaseStorage : public virtual storage::StateStorageInterface,
                    public virtual storage::MergeableStorageInterface
{
public:
    using Ptr = std::shared_ptr<BaseStorage<enableLRU>>;

    BaseStorage(std::shared_ptr<StorageInterface> prev, bool setRowWithDirtyFlag)
      : storage::StateStorageInterface(prev),
        m_buckets(std::thread::hardware_concurrency()),
        m_setRowWithDirtyFlag(setRowWithDirtyFlag)
    {}

    BaseStorage(const BaseStorage&) = delete;
    BaseStorage& operator=(const BaseStorage&) = delete;

    BaseStorage(BaseStorage&&) = delete;
    BaseStorage& operator=(BaseStorage&&) = delete;

    ~BaseStorage() override { m_recoder.clear(); }

    void asyncGetPrimaryKeys(std::string_view table,
        const std::optional<storage::Condition const>& condition,
        std::function<void(Error::UniquePtr, std::vector<std::string>)> _callback) override
    {
        std::map<std::string_view, storage::Entry::Status> localKeys;

        if (m_enableTraverse)
        {
            std::mutex mergeMutex;
            tbb::parallel_for(tbb::blocked_range<size_t>(0U, m_buckets.size()),
                [this, &mergeMutex, &localKeys, &table, &condition](auto const& range) {
                    for (auto i = range.begin(); i < range.end(); ++i)
                    {
                        auto& bucket = m_buckets[i];
                        std::unique_lock<std::mutex> lock(bucket.mutex);

                        decltype(localKeys) bucketKeys;
                        for (auto& it : bucket.container)
                        {
                            if (it.table == table && (!condition || condition->isValid(it.key)))
                            {
                                bucketKeys.emplace(it.key, it.entry.status());
                            }
                        }

                        std::unique_lock mergeLock(mergeMutex);
                        localKeys.merge(std::move(bucketKeys));
                    }
                });
        }

        auto prev = getPrev();
        if (!prev)
        {
            std::vector<std::string> resultKeys;
            for (auto& localIt : localKeys)
            {
                if (localIt.second == Entry::NORMAL || localIt.second == Entry::MODIFIED)
                {
                    resultKeys.push_back(std::string(localIt.first));
                }
            }

            _callback(nullptr, std::move(resultKeys));
            return;
        }

        prev->asyncGetPrimaryKeys(table, condition,
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
                        if (localIt->second == Entry::DELETED)
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
                    if (localIt.second == Entry::NORMAL || localIt.second == Entry::MODIFIED)
                    {
                        remoteKeys.push_back(std::string(localIt.first));
                    }
                }

                callback(nullptr, std::forward<decltype(remoteKeys)>(remoteKeys));
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

    void asyncGetRows(std::string_view tableView,
        RANGES::any_view<std::string_view,
            RANGES::category::input | RANGES::category::random_access | RANGES::category::sized>
            keys,
        std::function<void(Error::UniquePtr, std::vector<std::optional<Entry>>)> _callback) override
    {
        auto size = keys.size();
        std::vector<std::optional<Entry>> results(keys.size());
        auto missinges = std::tuple<std::vector<std::string_view>,
            std::vector<std::tuple<std::string, size_t>>>();

        std::atomic_ulong existsCount = 0;

        for (auto i = 0U; i < keys.size(); ++i)
        {
            auto [bucket, lock] = getBucket(tableView, keys[i]);
            boost::ignore_unused(lock);

            auto it = bucket->container.find(std::make_tuple(tableView, std::string_view(keys[i])));
            if (it != bucket->container.end())
            {
                auto& entry = it->entry;
                if (entry.status() == Entry::NORMAL || entry.status() == Entry::MODIFIED)
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
                std::get<1>(missinges).emplace_back(std::string(keys[i]), i);
                std::get<0>(missinges).emplace_back(keys[i]);
            }
        }

        auto prev = getPrev();
        if (existsCount < keys.size() && prev)
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

                    for (size_t i = 0; i < entries.size(); ++i)
                    {
                        auto& entry = entries[i];

                        if (entry)
                        {
                            results[std::get<1>(missingIndexes[i])].emplace(
                                importExistingEntry(table,
                                    std::move(std::get<0>(missingIndexes[i])), std::move(*entry)));
                        }
                    }

                    callback(nullptr, std::move(results));
                });
        }
        else
        {
            _callback(nullptr, std::move(results));
        }
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

        if (m_setRowWithDirtyFlag && entry.status() == Entry::NORMAL)
        {
            entry.setStatus(Entry::MODIFIED);
        }
        auto [bucket, lock] = getBucket(tableView, keyView);
        auto it = bucket->container.find(std::make_tuple(tableView, keyView));
        if (it != bucket->container.end())
        {
            auto& existsEntry = it->entry;
            entryOld.emplace(std::move(existsEntry));

            updatedCapacity -= entryOld->size();

            bucket->container.modify(it, [&entry](Data& data) { data.entry = std::move(entry); });
        }
        else
        {
            auto [iter, _] = bucket->container.emplace(
                Data{std::string(tableView), std::string(keyView), std::move(entry)});
            it = iter;
        }
        if constexpr (enableLRU)
        {
            updateMRUAndCheck(*bucket, it);
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
        std::lock_guard<std::mutex> lock(x_cacheMutex);
        tbb::parallel_for(tbb::blocked_range<size_t>(0, m_buckets.size()),
            [this, &onlyDirty, &callback](auto const& range) {
                for (auto i = range.begin(); i < range.end(); ++i)
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
            });
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

        STORAGE_LOG(INFO) << "Successful merged records" << LOG_KV("count", count);
    }

    crypto::HashType hash(
        const bcos::crypto::Hash::Ptr& hashImpl, const ledger::Features& features) const override
    {
        bcos::crypto::HashType totalHash;
        auto blockVersion = features.get(ledger::Features::Flag::bugfix_statestorage_hash) ?
                                (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION :
                                (uint32_t)bcos::protocol::BlockVersion::V3_0_VERSION;

        std::vector<bcos::crypto::HashType> hashes(m_buckets.size());
        tbb::parallel_for(tbb::blocked_range<size_t>(0U, m_buckets.size()), [&, this](
                                                                                auto const& range) {
            for (auto i = range.begin(); i < range.end(); ++i)
            {
                auto& bucket = m_buckets[i];

                bcos::crypto::HashType bucketHash(0);
                for (auto& it : bucket.container)
                {
                    auto& entry = it.entry;
                    if (entry.dirty())
                    {
                        bcos::crypto::HashType entryHash;
                        if (blockVersion >= (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION)
                        {
                            entryHash = entry.hash(it.table, it.key, *hashImpl, blockVersion);
                        }
                        else
                        {  // v3.0.0-v3.2.0 use this which will make it not compatible with
                           // v3.1.0 keyPageStorage
                            entryHash = hashImpl->hash(it.table) ^ hashImpl->hash(it.key) ^
                                        entry.hash(it.table, it.key, *hashImpl, blockVersion);
                        }
                        bucketHash ^= entryHash;
                    }
                }

                hashes[i] ^= bucketHash;
            }
        });

        for (auto const& it : hashes)
        {
            totalHash ^= it;
        }

        return totalHash;
    }


    void rollback(const Recoder& recoder) override
    {
        if (m_readOnly)
        {
            return;
        }

        for (const auto& change : recoder)
        {
            ssize_t updateCapacity = 0;
            auto [bucket, lock] = getBucket(change.table, std::string_view(change.key));
            boost::ignore_unused(lock);

            auto it = bucket->container.find(
                std::make_tuple(std::string_view(change.table), std::string_view(change.key)));
            if (change.entry)
            {
                if (it != bucket->container.end())
                {
                    if (c_fileLogLevel <= bcos::LogLevel::TRACE)
                    {
                        STORAGE_LOG(TRACE)
                            << "Revert exists: " << change.table << " | " << toHex(change.key)
                            << " | " << toHex(change.entry->get());
                    }

                    updateCapacity = change.entry->size() - it->entry.size();

                    const auto& rollbackEntry = change.entry;
                    bucket->container.modify(it,
                        [&rollbackEntry](Data& data) { data.entry = std::move(*rollbackEntry); });
                }
                else
                {
                    if (c_fileLogLevel <= bcos::LogLevel::TRACE)
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
                    if (c_fileLogLevel <= bcos::LogLevel::TRACE)
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
    void setMaxCapacity(ssize_t capacity) { m_maxCapacity = capacity; }

private:
    Entry importExistingEntry(std::string_view table, std::string_view key, Entry entry)
    {
        if (m_readOnly)
        {
            return entry;
        }
        if (x_cacheMutex.try_lock())
        {
            entry.setStatus(Entry::NORMAL);
            auto updateCapacity = entry.size();

            auto [bucket, lock] = getBucket(table, key);
            auto it = bucket->container.find(std::make_tuple(table, key));

            if (it == bucket->container.end())
            {
                it = bucket->container
                         .emplace(Data{std::string(table), std::string(key), std::move(entry)})
                         .first;

                bucket->capacity += updateCapacity;
            }
            else
            {
                STORAGE_LOG(DEBUG)
                    << "Fail import existsing entry, " << table << " | " << toHex(key);
            }
            x_cacheMutex.unlock();
            return it->entry;
        }
        return entry;
    }

    std::shared_ptr<StorageInterface> getPrev()
    {
        std::shared_lock<std::shared_mutex> lock(m_prevMutex);
        auto prev = m_prev;
        return prev;
    }

    bool m_enableTraverse = false;

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
    using Container = std::conditional_t<enableLRU, LRUHashContainer, HashContainer>;

    struct Bucket
    {
        Container container;
        std::mutex mutex;
        ssize_t capacity = 0;
    };
    uint32_t m_blockVersion = 0;
    std::vector<Bucket> m_buckets;
    bool m_setRowWithDirtyFlag = false;

    std::tuple<Bucket*, std::unique_lock<std::mutex>> getBucket(
        const std::string_view& table, const std::string_view& key)
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
