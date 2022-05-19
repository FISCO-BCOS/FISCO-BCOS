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
 * @brief KeyPage implementation
 * @file KeyPageStorage.cpp
 * @author: xingqiangbai
 * @date: 2022-04-19
 */
#include "KeyPageStorage.h"
#include <sstream>

namespace bcos::storage
{

void KeyPageStorage::asyncGetPrimaryKeys(std::string_view tableView,
    const std::optional<storage::Condition const>& _condition,
    std::function<void(Error::UniquePtr, std::vector<std::string>)> _callback)
{
    // if SYS_TABLES is not supported
    if (tableView == SYS_TABLES)
    {
        _callback(BCOS_ERROR_UNIQUE_PTR(StorageError::ReadError, "scan s_tables is not supported"),
            std::vector<std::string>());
        return;
    }
    if (!_condition)
    {
        _callback(BCOS_ERROR_UNIQUE_PTR(
                      StorageError::ReadError, "asyncGetPrimaryKeys must have condition"),
            std::vector<std::string>());
        return;
    }
    // page
    auto [error, data] = getData(tableView, "");
    if (error)
    {
        _callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(StorageError::ReadError,
                      std::string("get table meta data failed, table:").append(tableView), *error),
            std::vector<std::string>());
        return;
    }
    std::vector<std::string> ret;
    auto& meta = std::get<1>(data.value()->data);
    auto readLock = meta.rLock();
    auto pageInfo = meta.getAllPageInfoNoLock();
    auto [offset, total] = _condition->getLimit();
    ret.reserve(total);
    size_t validCount = 0;
    for (auto& info : pageInfo)
    {
        auto [error, data] = getData(tableView, info.startKey);
        boost::ignore_unused(error);
        assert(!error);
        auto& page = std::get<0>(data.value()->data);
        auto [entries, pageLock] = page.getEntries();
        boost::ignore_unused(pageLock);
        for (auto& it : entries)
        {
            if (it.second.status() != Entry::DELETED && _condition->isValid(it.first))
            {
                if (validCount >= offset && validCount < offset + total)
                {
                    ret.emplace_back(it.first);
                }
                ++validCount;
                if (validCount == offset + total)
                {
                    break;
                }
            }
        }
    }
    readLock.unlock();
    _callback(nullptr, std::move(ret));
}
// TODO: add interface and cow to avoid page copy
void KeyPageStorage::asyncGetRow(std::string_view tableView, std::string_view keyView,
    std::function<void(Error::UniquePtr, std::optional<Entry>)> _callback)
{
    // if sys table, read cache and read from prev, return
    if (tableView == SYS_TABLES)
    {
        auto [error, entry] = getRawEntry(tableView, keyView);
        if (error)
        {
            _callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(
                          StorageError::ReadError, "Get row from storage failed!", *error),
                {});
            return;
        }
        if (entry)
        {  // add cache, found
            _callback(nullptr, std::move(*entry));
        }
        else
        {  // not found
            _callback(nullptr, std::nullopt);
        }
        return;
    }

    auto [error, entry] = getEntryFromPage(tableView, keyView);
    _callback(std::move(error), std::move(entry));
}

void KeyPageStorage::asyncGetRows(std::string_view tableView,
    const std::variant<const gsl::span<std::string_view const>, const gsl::span<std::string const>>&
        _keys,
    std::function<void(Error::UniquePtr, std::vector<std::optional<Entry>>)> _callback)
{
    std::visit(
        [this, &tableView, &_callback](auto&& _keys) {
            std::vector<std::optional<Entry>> results(_keys.size());

            if (tableView == SYS_TABLES)
            {
                Error::UniquePtr err;
                // #pragma omp parallel for
                for (gsl::index i = 0; i < _keys.size(); ++i)
                {
                    auto [error, entry] = getRawEntry(tableView, _keys[i]);
                    if (error)
                    {
                        err = BCOS_ERROR_WITH_PREV_UNIQUE_PTR(
                            StorageError::ReadError, "get s_tables rows failed!", *error);
                        break;
                    }
                    results[i] = std::make_optional(std::move(*entry));
                }
                _callback(std::move(err), std::move(results));
            }
            else
            {  // page
                Error::UniquePtr err(nullptr);
                // TODO: because of page and lock, maybe not parallel is better
                for (gsl::index i = 0; i < _keys.size(); ++i)
                {
                    asyncGetRow(tableView, _keys[i],
                        [i, &results, &err](Error::UniquePtr _error, std::optional<Entry> _entry) {
                            if (_error)
                            {
                                err = BCOS_ERROR_WITH_PREV_UNIQUE_PTR(
                                    StorageError::ReadError, "get rows failed!", *_error);
                                return;
                            }
                            results[i] = std::move(_entry);
                        });
                    if (err)
                    {
                        break;
                    }
                }
                _callback(std::move(err), std::move(results));
            }
        },
        _keys);
}

void KeyPageStorage::asyncSetRow(std::string_view tableView, std::string_view keyView, Entry entry,
    std::function<void(Error::UniquePtr)> callback)
{  // delete is the same process as insert
    if (m_readOnly)
    {
        callback(
            BCOS_ERROR_UNIQUE_PTR(StorageError::ReadOnly, "Try to operate a read-only storage"));
        return;
    }

    // if sys table, write cache and write to prev, return
    if (tableView == SYS_TABLES)
    {
        std::optional<Entry> entryOld;

        auto [bucket, lock] = getMutBucket(tableView, keyView);
        boost::ignore_unused(lock);

        auto it =
            bucket->container.find(std::make_pair(std::string(tableView), std::string(keyView)));
        if (it != bucket->container.end())
        {  // update
            auto& existsEntry = it->second.entry;
            entryOld.emplace(std::move(existsEntry));
            it->second.entry = std::move(entry);
        }
        else
        {  // insert
            Data d{std::string(tableView), std::string(keyView), std::move(entry)};
            auto tableKey = std::make_pair(std::string(tableView), std::string(keyView));
            bucket->container.emplace(std::make_pair(std::move(tableKey), std::move(d)));
        }

        if (m_recoder.local())
        {
            m_recoder.local()->log(
                Recoder::Change(std::string(tableView), std::string(keyView), std::move(entryOld)));
        }
        lock.unlock();
        callback(nullptr);
    }
    else
    {
        auto error = setEntryToPage(std::string(tableView), std::string(keyView), std::move(entry));
        callback(std::move(error));
    }
}

void KeyPageStorage::parallelTraverse(bool onlyDirty,
    std::function<bool(
        const std::string_view& table, const std::string_view& key, const Entry& entry)>
        callback) const
{
#pragma omp parallel for
    for (size_t i = 0; i < m_buckets.size(); ++i)
    {
        auto& bucket = m_buckets[i];

        for (auto& it : bucket.container)
        {
            if (!onlyDirty || it.second.entry.dirty())
            {
                if (it.second.type == Data::Type::TableMeta)
                {  // if metadata

                    if (it.second.entry.status() == Entry::Status::EMPTY)
                    {  // empty table meta
                        continue;
                    }
                    else if (it.second.entry.status() == Entry::Status::NORMAL)
                    {  // if normal return entry without serialization
                        callback(it.first.first, it.first.second, it.second.entry);
                    }
                    else
                    {
                        auto& meta = std::get<1>(it.second.data);
                        auto readLock = meta.rLock();
                        Entry entry;
                        entry.setObject(meta);
                        readLock.unlock();
                        if (c_fileLogLevel >= bcos::LogLevel::TRACE)
                        {  // debug log to statistic the table pages
                            KeyPage_LOG(TRACE)
                                << LOG_DESC("Traverse TableMeta") << LOG_KV("table", it.first.first)
                                << LOG_KV("key", toHex(it.first.second))
                                << LOG_KV("size", entry.size()) << LOG_KV("meta", meta);
                        }
                        callback(it.first.first, it.first.second, std::move(entry));
                    }
                }
                else if (it.second.type == Data::Type::Page)
                {  // if page, encode and return
                    auto& page = std::get<0>(it.second.data);
                    Entry entry;
                    if (page.validCount() == 0)
                    {
                        entry.setStatus(Entry::Status::DELETED);
                        callback(it.first.first, it.first.second, std::move(entry));
                    }
                    else if (it.second.entry.status() == Entry::Status::NORMAL)
                    {
                        callback(it.first.first, it.first.second, it.second.entry);
                    }
                    else
                    {
                        entry.setObject(page);
                        entry.setStatus(it.second.entry.status());
                        KeyPage_LOG(DEBUG)
                            << LOG_DESC("Traverse Page") << LOG_KV("table", it.first.first)
                            << LOG_KV("key", toHex(it.first.second))
                            << LOG_KV("count", page.validCount())
                            << LOG_KV("status", (int)entry.status())
                            << LOG_KV("size", entry.size());
                        callback(it.first.first, it.first.second, std::move(entry));
                    }
                }
                else
                {
                    auto& entry = it.second.entry;
                    callback(it.first.first, it.first.second, entry);
                }
            }
        }
    }
}

crypto::HashType KeyPageStorage::hash(const bcos::crypto::Hash::Ptr& hashImpl) const
{
    bcos::crypto::HashType totalHash;

#pragma omp parallel for
    for (size_t i = 0; i < m_buckets.size(); ++i)
    {
        auto& bucket = m_buckets[i];
        bcos::crypto::HashType bucketHash;

        for (auto& it : bucket.container)
        {
            auto& entry = it.second.entry;
            if (entry.dirty() && it.second.type != Data::Type::TableMeta)
            {
                if (it.second.type == Data::Type::Page)
                {
                    auto& page = std::get<0>(it.second.data);
                    bucketHash ^= page.hash(it.second.table, hashImpl);
                }
                else
                {  // sys table
                    auto hash = hashImpl->hash(it.second.table);
                    hash ^= hashImpl->hash(it.second.key);
                    hash ^= entry.hash(it.second.table, it.second.key, hashImpl);
                    bucketHash ^= hash;
                }
            }
        }
#pragma omp critical
        totalHash ^= bucketHash;
    }
    return totalHash;
}

void KeyPageStorage::rollback(const Recoder& recoder)
{
    if (m_readOnly)
    {
        return;
    }

    for (auto& change : recoder)
    {
        if (change.table == SYS_TABLES)
        {
            auto [bucket, lock] = getMutBucket(change.table, change.key);
            boost::ignore_unused(lock);

            auto it = bucket->container.find(std::make_pair(change.table, change.key));
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
                    auto& rollbackEntry = change.entry;
                    it->second.entry = std::move(*rollbackEntry);
                }
                else
                {
                    if (c_fileLogLevel >= bcos::LogLevel::TRACE)
                    {
                        STORAGE_LOG(TRACE)
                            << "Revert deleted: " << change.table << " | " << toHex(change.key)
                            << " | " << toHex(change.entry->get());
                    }
                    Data d{change.table, change.key, std::move(*(change.entry))};
                    auto tableKey = std::make_pair(change.table, change.key);
                    bucket->container.emplace(std::make_pair(std::move(tableKey), std::move(d)));
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
        }
        else
        {  // page entry

            auto [error, data] = getData(change.table, "");
            if (error)
            {
                BOOST_THROW_EXCEPTION(*error);
            }
            auto& meta = std::get<1>(data.value()->data);
            auto writeLock = meta.lock();
            auto pageKey = meta.getPageKeyNoLock(change.key);
            if (pageKey)
            {
                auto [error, data] = getData(change.table, pageKey.value());
                if (error || !data)
                {
                    BOOST_THROW_EXCEPTION(*error);
                }
                auto& page = std::get<0>(data.value()->data);
                page.rollback(change);
                // revert also need update pageInfo
                auto oldStartKey = meta.updatePageInfoNoLock(pageKey.value(), page.startKey(),
                    page.endKey(), page.validCount(), page.size());
                if (oldStartKey)
                {
                    changePageKey(change.table, oldStartKey.value(), page.startKey());
                }
            }
            else
            {
                auto message =
                    (boost::format("Not found rollback page: %s:%s") % change.table % change.key)
                        .str();
                BOOST_THROW_EXCEPTION(BCOS_ERROR(StorageError::ReadError, message));
            }
        }
    }
}

}  // namespace bcos::storage
