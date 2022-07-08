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
    if (m_ignoreTables->find(tableView) != m_ignoreTables->end())
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
    auto [error, data] = getData(tableView, TABLE_META_KEY);
    if (error)
    {
        _callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(StorageError::ReadError,
                      std::string("get table meta data failed, table:").append(tableView), *error),
            std::vector<std::string>());
        return;
    }
    std::vector<std::string> ret;
    auto meta = &std::get<1>(data.value()->data);
    auto readLock = meta->rLock();
    auto& pageInfo = meta->getAllPageInfoNoLock();
    auto [offset, total] = _condition->getLimit();
    ret.reserve(total);
    size_t validCount = 0;
    for (auto& info : pageInfo)
    {
        auto [error, data] = getData(tableView, info.getPageKey(), true);
        boost::ignore_unused(error);
        assert(!error);
        auto page = &std::get<0>(data.value()->data);
        auto [entries, pageLock] = page->getEntries();
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
    if (m_ignoreTables->find(tableView) != m_ignoreTables->end())
    {
        auto [error, entry] = getSysTableRawEntry(tableView, keyView);
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

            if (m_ignoreTables->find(tableView) != m_ignoreTables->end())
            {
                Error::UniquePtr err;
                // #pragma omp parallel for
                for (gsl::index i = 0; i < _keys.size(); ++i)
                {
                    auto [error, entry] = getSysTableRawEntry(tableView, _keys[i]);
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
    if (m_ignoreTables->find(tableView) != m_ignoreTables->end())
    {
        std::optional<Entry> entryOld;

        auto [bucket, lock] = getMutBucket(tableView, keyView);
        boost::ignore_unused(lock);
        entry.setStatus(Entry::Status::MODIFIED);
        auto it =
            bucket->container.find(std::make_pair(std::string(tableView), std::string(keyView)));
        if (it != bucket->container.end())
        {  // update
            auto& existsEntry = it->second->entry;
            entryOld.emplace(std::move(existsEntry));

            it->second->entry = std::move(entry);
        }
        else
        {  // insert
            auto tableKey = std::make_pair(std::string(tableView), std::string(keyView));
            bucket->container.emplace(std::make_pair(std::move(tableKey),
                std::make_shared<Data>(std::string(tableView), std::string(keyView),
                    std::move(entry), Data::Type::NormalEntry)));
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
            if (it.second->type == Data::Type::TableMeta)
            {  // if metadata
                if (!onlyDirty || it.second->entry.dirty())
                {
                    auto meta = &std::get<1>(it.second->data);
                    auto readLock = meta->rLock();
                    Entry entry;
                    entry.setObject(*meta);
                    readLock.unlock();
                    if (c_fileLogLevel >= bcos::LogLevel::TRACE)
                    {  // FIXME: this log is only for debug, comment it when release
                        KeyPage_LOG(TRACE)
                            << LOG_DESC("TableMeta") << LOG_KV("table", it.first.first)
                            << LOG_KV("key", toHex(it.first.second)) << LOG_KV("meta", *meta);
                    }

                    KeyPage_LOG(DEBUG)
                        << LOG_DESC("Traverse TableMeta") << LOG_KV("table", it.first.first)
                        << LOG_KV("count", meta->size()) << LOG_KV("size", entry.size())
                        << LOG_KV("count", meta->size())
                        << LOG_KV("payloadRate",
                               sizeof(PageInfo) * meta->size() / (double)entry.size())
                        << LOG_KV("predictHit", meta->hitRate());
                    callback(it.first.first, it.first.second, std::move(entry));
                }
            }
            else if (it.second->type == Data::Type::Page)
            {  // if page, encode and return
                auto page = &std::get<0>(it.second->data);
                if (!onlyDirty || it.second->entry.dirty())
                {
                    Entry entry;
                    if (page->validCount() == 0)
                    {
                        KeyPage_LOG(DEBUG)
                            << LOG_DESC("Traverse deleted Page") << LOG_KV("table", it.first.first)
                            << LOG_KV("key", toHex(it.first.second))
                            << LOG_KV("validCount", page->validCount());
                        entry.setStatus(Entry::Status::DELETED);
                        callback(it.first.first, it.first.second, std::move(entry));
                    }
                    else
                    {
                        entry.setObject(*page);
                        entry.setStatus(it.second->entry.status());
                        if (c_fileLogLevel >= TRACE)
                        {
                            KeyPage_LOG(TRACE)
                                << LOG_DESC("Traverse Page") << LOG_KV("table", it.first.first)
                                << LOG_KV("pageKey", toHex(it.first.second))
                                << LOG_KV("validCount", page->validCount())
                                << LOG_KV("count", page->count())
                                << LOG_KV("status", (int)it.second->entry.status())
                                << LOG_KV("pageSize", page->size()) << LOG_KV("size", entry.size());
                        }
                        if (it.first.second != page->endKey())
                        {
                            KeyPage_LOG(WARNING)
                                << LOG_DESC("Traverse Page pageKey not equal to map key")
                                << LOG_KV("table", it.first.first)
                                << LOG_KV("pageKey", page->endKey())
                                << LOG_KV("mapKey", it.first.second);
                        }
                        callback(it.first.first, it.first.second, std::move(entry));
                    }
                    auto invalidKeys = page->invalidKeySet();
                    for (auto& k : invalidKeys)
                    {
                        if (c_fileLogLevel >= TRACE)
                        {
                            KeyPage_LOG(TRACE)
                                << LOG_DESC("Traverse Page delete invalid key")
                                << LOG_KV("table", it.first.first) << LOG_KV("key", toHex(k));
                        }
                        Entry e;
                        e.setStatus(Entry::Status::DELETED);
                        callback(it.first.first, k, std::move(e));
                    }
                }
            }
            else
            {
                if (!onlyDirty || it.second->entry.dirty())
                {
                    auto& entry = it.second->entry;
                    // assert(it.first.first == SYS_TABLES);
                    callback(it.first.first, it.first.second, entry);
                }
            }
        }
    }
}

crypto::HashType KeyPageStorage::hash(const bcos::crypto::Hash::Ptr& hashImpl) const
{
    bcos::crypto::HashType totalHash(0);
    std::vector<const Data*> allData;
    for (size_t i = 0; i < m_buckets.size(); ++i)
    {
        auto& bucket = m_buckets[i];
        for (auto& it : bucket.container)
        {
            allData.push_back(it.second.get());
        }
    }
#pragma omp parallel for
    for (size_t i = 0; i < allData.size(); ++i)
    {
        auto data = allData[i];
        auto& entry = data->entry;
        if (entry.dirty() && data->type != Data::Type::TableMeta)
        {
            if (data->type == Data::Type::Page)
            {
                auto page = &std::get<0>(data->data);
                auto pageHash = page->hash(data->table, hashImpl);
#pragma omp critical
                totalHash ^= pageHash;
            }
            else
            {  // sys table
                auto hash = hashImpl->hash(data->table);
                hash ^= hashImpl->hash(data->key);
                hash ^= entry.hash(data->table, data->key, hashImpl);
#pragma omp critical
                totalHash ^= hash;
            }
        }
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
        if (m_ignoreTables->find(change.table) != m_ignoreTables->end())
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
                    it->second->entry = std::move(*rollbackEntry);
                }
                else
                {
                    if (c_fileLogLevel >= bcos::LogLevel::TRACE)
                    {
                        STORAGE_LOG(TRACE)
                            << "Revert deleted: " << change.table << " | " << toHex(change.key)
                            << " | " << toHex(change.entry->get());
                    }
                    auto tableKey = std::make_pair(change.table, change.key);
                    bucket->container.emplace(std::make_pair(std::move(tableKey),
                        std::make_shared<Data>(change.table, change.key, std::move(*(change.entry)),
                            Data::Type::NormalEntry)));
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

            auto [error, data] = getData(change.table, TABLE_META_KEY);
            if (error)
            {
                BOOST_THROW_EXCEPTION(*error);
            }
            auto meta = &std::get<1>(data.value()->data);
            auto writeLock = meta->lock();
            auto pageInfoOp = meta->getPageInfoNoLock(change.key);
            if (pageInfoOp)
            {
                auto pageKey = pageInfoOp.value()->getPageKey();
                auto pageData = pageInfoOp.value()->getPageData();
                if (!pageData)
                {
                    auto [error, pageDataOp] = getData(change.table, pageKey, true);
                    if (error || !pageDataOp)
                    {
                        BOOST_THROW_EXCEPTION(*error);
                    }
                    pageData = pageDataOp.value();
                }
                auto page = &std::get<0>(pageData->data);
                page->rollback(change);
                if (page->count() == 0)
                {  // page is empty because of rollback, means it it first created
                    pageData->entry.setStatus(Entry::Status::EMPTY);
                }
                // revert also need update pageInfo
                auto oldStartKey = meta->updatePageInfoNoLock(
                    pageKey, page->endKey(), page->validCount(), page->size(), pageInfoOp);
                if (oldStartKey)
                {
                    changePageKey(change.table, oldStartKey.value(), page->endKey());
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

// if data not exist, create an empty one
std::tuple<Error::UniquePtr, std::optional<KeyPageStorage::Data*>> KeyPageStorage::getData(
    std::string_view tableView, std::string_view key, bool mustExist)
{
    // find from cache
    auto [bucket, lock] = getBucket(tableView, key);
    boost::ignore_unused(lock);
    auto keyPair = std::make_pair(std::string(tableView), std::string(key));
    decltype(bucket->container)::iterator it = bucket->container.find(keyPair);
    if (it != bucket->container.end())
    {
        // assert(it->first.second == key);
        auto data = it->second.get();
        lock.unlock();
        return std::make_tuple(std::unique_ptr<Error>(nullptr), std::make_optional(data));
    }
    lock.unlock();
    // find from prev
    auto d = std::make_shared<Data>();
    d->table = std::string(tableView);
    d->key = std::string(key);
    do
    {
        auto prevKeyPage = std::dynamic_pointer_cast<bcos::storage::KeyPageStorage>(getPrev());
        if (prevKeyPage)
        {
            auto dataOption = prevKeyPage->copyData(tableView, key);
            if (c_fileLogLevel >= TRACE)
            {
                KeyPage_LOG(TRACE)
                    << LOG_DESC("get data from KeyPageStorage") << LOG_KV("table", tableView)
                    << LOG_KV("key", toHex(key)) << LOG_KV("found", dataOption ? true : false);
            }
            if (dataOption)
            {
                d = std::move(*dataOption);
                if (!d->key.empty())
                {  // set entry to clean
                    auto page = &std::get<0>(d->data);
                    page->clean(d->key);
                    if (c_fileLogLevel >= TRACE)
                    {
                        KeyPage_LOG(TRACE)
                            << LOG_DESC("import page") << LOG_KV("table", tableView)
                            << LOG_KV("key", toHex(key)) << LOG_KV("validCount", page->validCount())
                            << LOG_KV("count", page->count());
                    }
                }
                else
                {
                    auto meta = &std::get<1>(d->data);
                    meta->clean();
                    if (c_fileLogLevel >= TRACE)
                    {
                        KeyPage_LOG(TRACE)
                            << LOG_DESC("import TableMeta") << LOG_KV("table", tableView)
                            << LOG_KV("size", meta->size()) << LOG_KV("meta", *meta);
                    }
                }
                d->entry.setStatus(Entry::Status::NORMAL);
                break;
            }
        }
        else
        {
            auto [error, entry] = getRawEntryFromStorage(tableView, key);
            if (error)
            {
                KeyPage_LOG(ERROR)
                    << LOG_DESC("getData error") << LOG_KV("table", tableView)
                    << LOG_KV("key", toHex(key)) << LOG_KV("error", error->errorMessage());
                return std::make_tuple(std::move(error), std::nullopt);
            }
            if (c_fileLogLevel >= TRACE)
            {
                KeyPage_LOG(TRACE)
                    << LOG_DESC("get data from storage") << LOG_KV("table", tableView)
                    << LOG_KV("key", toHex(key)) << LOG_KV("found", entry ? true : false);
            }
            if (entry)
            {
                entry->setStatus(Entry::Status::NORMAL);
                d = std::make_shared<Data>(std::string(tableView), std::string(key),
                    std::move(*entry), key.empty() ? Data::Type::TableMeta : Data::Type::Page);
                break;
            }
        }
        if (mustExist)
        {
            KeyPage_LOG(FATAL) << LOG_DESC("data should exist") << LOG_KV("table", tableView)
                               << LOG_KV("key", toHex(key));
        }
        if (c_fileLogLevel >= TRACE)
        {
            KeyPage_LOG(TRACE) << LOG_DESC("create empty data") << LOG_KV("table", tableView)
                               << LOG_KV("key", toHex(key));
        }
        if (key.empty())
        {
            d->data = KeyPageStorage::TableMeta();
            d->type = Data::Type::TableMeta;
        }
        else
        {
            d->data = KeyPageStorage::Page();
            d->type = Data::Type::Page;
        }
        d->entry.setStatus(Entry::Status::EMPTY);
    } while (0);

    {  // insert into cache
        auto [bucket, writeLock] = getMutBucket(d->table, d->key);
        boost::ignore_unused(writeLock);
        auto data =
            bucket->container.emplace(std::make_pair(keyPair, std::move(d))).first->second.get();
        return std::make_tuple(nullptr, std::make_optional(data));
    }
}

std::pair<Error::UniquePtr, std::optional<Entry>> KeyPageStorage::getEntryFromPage(
    std::string_view table, std::string_view key)
{
    // key is empty means the data is TableMeta
    auto [error, data] = getData(table, TABLE_META_KEY);
    if (error)
    {
        return std::make_pair(std::move(error), std::nullopt);
    }
    auto meta = &std::get<1>(data.value()->data);
    auto readLock = meta->rLock();
    if (key.empty())
    {  // table meta
        if (meta->size() > 0)
        {
            if (c_fileLogLevel >= TRACE)
            {
                KeyPage_LOG(TRACE) << LOG_DESC("return meta entry") << LOG_KV("table", table)
                                   << LOG_KV("size", meta->size())
                                   << LOG_KV("status", int(data.value()->entry.status()))
                                   << LOG_KV("dirty", data.value()->entry.dirty());
            }
            if (data.value()->entry.dirty())
            {
                Entry entry;
                entry.setObject(*meta);
                entry.setStatus(data.value()->entry.status());
                return std::make_pair(nullptr, std::move(entry));
            }
            else
            {
                return std::make_pair(nullptr, data.value()->entry);
            }
        }
        return std::make_pair(nullptr, std::nullopt);
    }
    auto pageInfoOp = meta->getPageInfoNoLock(key);
    if (pageInfoOp)
    {
        Data* pageData = pageInfoOp.value()->getPageData();
        if (!pageData)
        {
            auto [error, pageDataOp] = getData(table, pageInfoOp.value()->getPageKey(), true);
            if (error)
            {
                return std::make_pair(std::move(error), std::nullopt);
            }
            pageData = pageDataOp.value();
            pageInfoOp.value()->setPageData(pageData);
        }
        if (pageData->entry.status() == Entry::Status::EMPTY)
        {
            return std::make_pair(nullptr, std::nullopt);
        }
        auto page = &std::get<0>(pageData->data);
        if (m_readOnly)
        {  // TODO: check condition, if key is pageKey, return page
            if (pageInfoOp.value()->getPageKey() != key)
            {
                KeyPage_LOG(FATAL)
                    << LOG_DESC("getEntryFromPage readonly try to read entry(should read page)")
                    << LOG_KV("pageKey", toHex(pageInfoOp.value()->getPageKey()))
                    << LOG_KV("key", toHex(key));
            }
            if (page->size() > 0)
            {
                auto pageReadLock = page->rLock();
                if (c_fileLogLevel >= TRACE)
                {
                    KeyPage_LOG(TRACE)
                        << LOG_DESC("return page entry") << LOG_KV("table", table)
                        << LOG_KV("startKey", toHex(page->startKey()))
                        << LOG_KV("EndKey", toHex(page->endKey()))
                        << LOG_KV("pageSize", page->size()) << LOG_KV("valid", page->validCount())
                        << LOG_KV("count", page->count())
                        << LOG_KV("dirty", data.value()->entry.dirty());
                }
                Entry entry;
                entry.setObject(*page);
                entry.setStatus(pageData->entry.status());
                return std::make_pair(nullptr, std::move(entry));
            }
            return std::make_pair(nullptr, std::nullopt);
        }
        auto entry = page->getEntry(key);
        // if (c_fileLogLevel >= TRACE)
        // {  // FIXME: this log is only for debug, comment it when release
        //     KeyPage_LOG(TRACE) << LOG_DESC("getEntry from page") << LOG_KV("table", table)
        //                        << LOG_KV("pageKey", toHex(pageKey.value()))
        //                        << LOG_KV("key", toHex(key))
        //                        << LOG_KV("value", entry ? toHex(entry->get()) : "Not found");
        // }
        return std::make_pair(nullptr, std::move(entry));
    }
    return std::make_pair(nullptr, std::nullopt);
}

Error::UniquePtr KeyPageStorage::setEntryToPage(std::string table, std::string key, Entry entry)
{
    auto [error, data] = getData(table, TABLE_META_KEY);
    if (error)
    {
        return std::move(error);
    }
    auto meta = &std::get<1>(data.value()->data);
    auto metaWriteLock = meta->lock();
    // insert or update
    auto pageInfoOption = meta->getPageInfoNoLock(key);
    std::string pageKey = key;
    Data* pageData = nullptr;
    if (pageInfoOption)
    {
        pageKey = pageInfoOption.value()->getPageKey();
        pageData = pageInfoOption.value()->getPageData();
    }
    std::optional<Entry> entryOld;
    if (!pageData)
    {
        auto [e, pageDataOp] = getData(table, pageKey, pageInfoOption.has_value());
        if (e)
        {
            return std::move(e);
        }
        pageData = pageDataOp.value();
        if (pageInfoOption)
        {
            pageInfoOption.value()->setPageData(pageData);
        }
    }
    // if new entry is too big, it will trigger split
    auto page = &std::get<0>(pageData->data);
    {
        auto ret = page->setEntry(key, std::move(entry));
        entryOld = std::move(std::get<0>(ret));
        auto pageInfoChanged = std::get<1>(ret);

        if (pageInfoChanged)
        {
            if (pageData->entry.status() == Entry::Status::EMPTY)
            {  // new page insert, if entries is empty means page delete entry which not exist
                meta->insertPageInfoNoLock(PageInfo(page->endKey(), (uint16_t)page->validCount(),
                    (uint16_t)page->size(), pageData));
                // pageData->entry.setStatus(Entry::Status::NORMAL);
                pageData->entry.setStatus(Entry::Status::MODIFIED);
            }
            else
            {
                auto oldStartKey = meta->updatePageInfoNoLock(
                    pageKey, page->endKey(), page->validCount(), page->size(), pageInfoOption);
                pageData->entry.setStatus(Entry::Status::MODIFIED);
                if (oldStartKey)
                {  // the page key is changed, 1. delete the first key, 2. insert a smaller key
                    // if the startKey of page changed, the container also need to be updated
                    if (page->validCount() > 0)
                    {
                        pageData = changePageKey(table, oldStartKey.value(), page->endKey());
                        page = &std::get<0>(pageData->data);
                    }
                    else
                    {  // page is empty because delete, not update startKey and mark as deleted
                        pageData->entry.setStatus(Entry::Status::DELETED);
                    }
                }
            }
        }
        else
        {
            pageData->entry.setStatus(Entry::Status::MODIFIED);
        }
        // page is modified, the meta maybe modified, mark meta as dirty
        data.value()->entry.setStatus(Entry::Status::MODIFIED);
    }
    pageKey = page->endKey();
    if (page->size() > m_pageSize && page->validCount() > 1)
    {  // split page, TODO: if dag trigger split, it maybe split to different page?
        if (c_fileLogLevel >= TRACE)
        {
            KeyPage_LOG(TRACE) << LOG_DESC("trigger split page") << LOG_KV("table", table)
                               << LOG_KV("pageKey", toHex(pageKey)) << LOG_KV("size", page->size())
                               << LOG_KV("m_pageSize", m_pageSize)
                               << LOG_KV("validCount", page->validCount())
                               << LOG_KV("count", page->count());
        }
        auto newPage = page->split(m_splitSize);
        // update old meta pageInfo
        auto oldStartKey = meta->updatePageInfoNoLock(
            pageKey, page->endKey(), page->validCount(), page->size(), pageInfoOption);
        if (oldStartKey)
        {  // if the startKey of page changed, the container also need to be updated
            pageData = changePageKey(table, oldStartKey.value(), page->endKey());
            page = &std::get<0>(pageData->data);
        }

        KeyPage_LOG(DEBUG) << LOG_DESC("split page finished") << LOG_KV("table", table)
                           << LOG_KV("pageKey", toHex(page->endKey()))
                           << LOG_KV("newPageKey", toHex(newPage.endKey()))
                           << LOG_KV("validCount", page->validCount())
                           << LOG_KV("newValidCount", newPage.validCount())
                           << LOG_KV("count", page->count()) << LOG_KV("newCount", newPage.count())
                           << LOG_KV("pageSize", page->size())
                           << LOG_KV("newPageSize", newPage.size());
        // insert new page to container, newPageInfo to meta
        insertNewPage(table, newPage.endKey(), meta, std::move(newPage));
        data.value()->entry.setStatus(Entry::Status::MODIFIED);
    }
    else if (page->size() < m_mergeSize)
    {  // merge operation
        // get next page, check size and merge current into next
        auto nextPageKey = meta->getNextPageKeyNoLock(page->endKey());
        if (nextPageKey)
        {
            auto [error, nextPageData] = getData(table, nextPageKey.value());
            boost::ignore_unused(error);
            if (error)
            {
                KeyPage_LOG(FATAL)
                    << LOG_DESC("merge page getData error") << LOG_KV("table", table)
                    << LOG_KV("key", toHex(key)) << LOG_KV("pageKey", toHex(pageKey));
            }
            auto nextPage = &std::get<0>(nextPageData.value()->data);
            if (nextPage->size() < m_splitSize && nextPage != page)
            {
                auto endKey = page->endKey();
                auto nextEndKey = nextPage->endKey();
                KeyPage_LOG(DEBUG)
                    << LOG_DESC("merge current page into next") << LOG_KV("table", table)
                    << LOG_KV("key", toHex(key)) << LOG_KV("pageKey", toHex(pageKey))
                    << LOG_KV("pageEnd", toHex(endKey)) << LOG_KV("pageCount", page->validCount())
                    << LOG_KV("pageSize", page->size()) << LOG_KV("nextPageKey", toHex(nextEndKey))
                    << LOG_KV("nextPageCount", nextPage->validCount())
                    << LOG_KV("nextPageSize", nextPage->size());
                nextPage->merge(*page);
                // remove current page info and update next page info
                auto nextPageInfoOp = meta->deletePageInfoNoLock(endKey, pageInfoOption);
                auto oldStartKey = meta->updatePageInfoNoLock(nextEndKey, nextPage->endKey(),
                    nextPage->validCount(), nextPage->size(), nextPageInfoOp);
                // old page also need write to disk to clean data, so not remove old page
                // nextPageData.value()->entry.setDirty(true);
                // pageData->entry.setDirty(true);
                nextPageData.value()->entry.setStatus(Entry::Status::MODIFIED);
                pageData->entry.setStatus(Entry::Status::DELETED);
                if (oldStartKey)
                {  // if the startKey of nextPage changed, the container also need to be updated
                    changePageKey(table, oldStartKey.value(), nextPage->endKey());
                }
                data.value()->entry.setStatus(Entry::Status::MODIFIED);
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

std::pair<Error::UniquePtr, std::optional<Entry>> KeyPageStorage::getRawEntryFromStorage(
    std::string_view table, std::string_view key)
{
    auto prev = getPrev();  // prev must not null
    assert(prev);
    if (!prev)
    {
        KeyPage_LOG(FATAL) << LOG_DESC("previous stortage is null");
        return std::make_pair(nullptr, std::nullopt);
    }
    std::promise<std::pair<Error::UniquePtr, std::optional<Entry>>> getPromise;
    prev->asyncGetRow(table, key, [&](Error::UniquePtr error, std::optional<Entry> entry) {
        KeyPage_LOG(TRACE) << LOG_DESC("getEntry from previous") << LOG_KV("table", table)
                           << LOG_KV("key", toHex(key))
                           << LOG_KV("size", entry ? entry->size() : 0);
        getPromise.set_value({std::move(error), std::move(entry)});
    });
    return getPromise.get_future().get();
}
std::pair<Error::UniquePtr, std::optional<Entry>> KeyPageStorage::getSysTableRawEntry(
    std::string_view table, std::string_view key)
{
    auto [bucket, lock] = getBucket(table, key);
    boost::ignore_unused(lock);
    auto it = bucket->container.find(std::make_pair(std::string(table), std::string(key)));
    if (it != bucket->container.end())
    {
        auto& entry = it->second->entry;
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

Entry KeyPageStorage::importExistingEntry(std::string_view table, std::string_view key, Entry entry)
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
        auto d = std::make_shared<Data>(
            std::string(table), std::string(key), std::move(entry), Data::Type::NormalEntry);
        auto tableKey = std::make_pair(std::string(table), std::string(key));
        it = bucket->container.emplace(std::make_pair(std::move(tableKey), std::move(d))).first;
    }
    else
    {
        KeyPage_LOG(WARNING) << "Fail import existsing entry, " << table << " | " << toHex(key);
    }

    return it->second->entry;
}
}  // namespace bcos::storage
