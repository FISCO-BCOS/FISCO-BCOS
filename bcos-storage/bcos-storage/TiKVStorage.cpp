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
 * @brief the implement of TiKVStorage
 * @file TiKVStorage.h
 * @author: xingqiangbai
 * @date: 2021-09-26
 */
#include "TiKVStorage.h"
#include "Common.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-framework/storage/Table.h"
#include "tikv_client.h"
#include <bcos-utilities/Error.h>
#include <tbb/concurrent_vector.h>
#include <tbb/parallel_for.h>
#include <tbb/spin_mutex.h>
#include <atomic>
#include <exception>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>

constexpr uint32_t MAX_RETRY_LIMIT = 32;

using namespace bcos::storage;
using namespace bcos::protocol;
using namespace std;

#define STORAGE_TIKV_LOG(LEVEL) BCOS_LOG(LEVEL) << "[STORAGE-TiKV]"
namespace bcos::storage
{

std::shared_ptr<tikv_client::TransactionClient> newTiKVClient(
    const std::vector<std::string>& pdAddrs, const std::string& logPath, const std::string& caPath,
    const std::string& certPath, const std::string& keyPath, uint32_t grpcTimeout)
{
    if (!caPath.empty() && !certPath.empty() && !keyPath.empty())
    {
        STORAGE_TIKV_LOG(INFO) << LOG_DESC("newTiKVClientWithSSL") << LOG_KV("logPath", logPath);
        return std::make_shared<tikv_client::TransactionClient>(
            pdAddrs, logPath, caPath, certPath, keyPath);
    }
    STORAGE_TIKV_LOG(INFO) << LOG_DESC("newTiKVClient") << LOG_KV("logPath", logPath);
    return std::make_shared<tikv_client::TransactionClient>(pdAddrs, logPath, grpcTimeout);
}
}  // namespace bcos::storage

TiKVStorage::TiKVStorage(
    std::shared_ptr<tikv_client::TransactionClient> _cluster, int32_t _commitTimeout)
  : m_cluster(std::move(_cluster)),
    m_lastCommitTimestamp(m_cluster->current_timestamp()),
    m_commitTimeout(_commitTimeout)
{}

void TiKVStorage::asyncGetPrimaryKeys(std::string_view _table,
    const std::optional<Condition const>& _condition,
    std::function<void(Error::UniquePtr, std::vector<std::string>)> _callback) noexcept
{
    try
    {
        auto start = utcTime();
        std::vector<std::string> result;

        std::string keyPrefix;
        keyPrefix = string(_table) + TABLE_KEY_SPLIT;
        // snapshot is not threadsafe so create it every time
        auto snap = m_cluster->snapshot();
        // TODO: check performance and add limit of primary keys
        bool finished = false;
        auto lastKey = keyPrefix;
        int i = 0;
        while (!finished)
        {
            auto keys = snap->scan_keys(
                lastKey, Bound::Excluded, string(), Bound::Unbounded, scan_batch_size);
            if (keys.empty())
            {
                finished = true;
                break;
            }
            lastKey = keys.back();
            for (auto& key : keys)
            {
                if (key.rfind(keyPrefix, 0) == 0)
                {
                    size_t start = keyPrefix.size();
                    auto realKey = key.substr(start);
                    if (!_condition || _condition->isValid(realKey))
                    {  // filter by condition, remove keyPrefix
                        result.push_back(std::move(realKey));
                    }
                }
                else
                {
                    finished = true;
                    break;
                }
            }
        }
        auto end = utcTime();
        STORAGE_TIKV_LOG(DEBUG) << LOG_DESC("asyncGetPrimaryKeys") << LOG_KV("table", _table)
                                << LOG_KV("count", result.size())
                                << LOG_KV("read time(ms)", end - start)
                                << LOG_KV("callback time(ms)", utcTime() - end);
        _callback(nullptr, std::move(result));
    }
    catch (const std::exception& e)
    {
        STORAGE_TIKV_LOG(WARNING) << LOG_DESC("asyncGetPrimaryKeys failed, need trigger switch")
                                  << LOG_KV("table", _table) << LOG_KV("message", e.what());
        _callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(ReadError, "asyncGetPrimaryKeys failed!", e), {});
        triggerSwitch();
    }
}

void TiKVStorage::asyncGetRow(std::string_view _table, std::string_view _key,
    std::function<void(Error::UniquePtr, std::optional<Entry>)> _callback) noexcept
{
    try
    {
        if (!isValid(_table, _key))
        {
            STORAGE_TIKV_LOG(WARNING) << LOG_DESC("asyncGetRow empty tableName or key")
                                      << LOG_KV("table", _table) << LOG_KV("key", toHex(_key));
            _callback(BCOS_ERROR_UNIQUE_PTR(TableNotExists, "empty tableName or key"), {});
            return;
        }
        auto start = utcTime();
        auto dbKey = toDBKey(_table, _key);
        auto snap = m_cluster->snapshot();
        auto value = snap->get(dbKey);
        auto end = utcTime();
        if (!value.has_value())
        {
            if (c_fileLogLevel <= TRACE)
            {
                STORAGE_TIKV_LOG(TRACE) << LOG_DESC("asyncGetRow empty") << LOG_KV("table", _table)
                                        << LOG_KV("key", toHex(_key)) << LOG_KV("dbKey", dbKey);
            }
            _callback(nullptr, {});
            return;
        }

        auto entry = std::make_optional<Entry>();
        entry->set(value.value());
        if (c_fileLogLevel <= TRACE)
        {
            STORAGE_TIKV_LOG(TRACE)
                << LOG_DESC("asyncGetRow") << LOG_KV("table", _table) << LOG_KV("key", toHex(_key))
                << LOG_KV("read time(ms)", end - start)
                << LOG_KV("callback time(ms)", utcTime() - end);
        }
        _callback(nullptr, std::move(entry));
    }
    catch (const std::exception& e)
    {
        STORAGE_TIKV_LOG(WARNING) << LOG_DESC("asyncGetRow failed, need trigger switch")
                                  << LOG_KV("table", _table) << LOG_KV("key", toHex(_key))
                                  << LOG_KV("message", e.what());
        _callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(ReadError, "asyncGetRow failed!", e), {});
        triggerSwitch();
    }
}

void TiKVStorage::asyncGetRows(std::string_view _table,
    const std::variant<const gsl::span<std::string_view const>, const gsl::span<std::string const>>&
        _keys,
    std::function<void(Error::UniquePtr, std::vector<std::optional<Entry>>)> _callback) noexcept
{
    try
    {
        if (!isValid(_table))
        {
            STORAGE_TIKV_LOG(WARNING)
                << LOG_DESC("asyncGetRows empty tableName") << LOG_KV("table", _table);
            _callback(BCOS_ERROR_UNIQUE_PTR(TableNotExists, "empty tableName"), {});
            return;
        }
        auto start = utcTime();
        std::visit(
            [&](auto const& keys) {
                std::vector<std::optional<Entry>> entries(keys.size());

                std::vector<std::string> realKeys(keys.size());
                tbb::parallel_for(tbb::blocked_range<size_t>(0, keys.size()),
                    [&](const tbb::blocked_range<size_t>& range) {
                        for (size_t i = range.begin(); i != range.end(); ++i)
                        {
                            realKeys[i] = toDBKey(_table, keys[i]);
                        }
                    });
                auto snap = m_cluster->snapshot();
                auto result = snap->batch_get(realKeys);
                auto end = utcTime();
                size_t validCount = 0;
                for (size_t i = 0; i < realKeys.size(); ++i)
                {
                    auto node = result.extract(realKeys[i]);
                    if (node.empty() || node.mapped().empty())
                    {
                        entries[i] = std::nullopt;
                        STORAGE_TIKV_LOG(TRACE) << "Multi get rows, not found key: " << keys[i];
                    }
                    else
                    {
                        ++validCount;
                        entries[i] = std::make_optional(Entry());
                        entries[i]->set(std::move(node.mapped()));
                    }
                }
                auto decode = utcTime();
                STORAGE_TIKV_LOG(DEBUG)
                    << LOG_DESC("asyncGetRows") << LOG_KV("table", _table)
                    << LOG_KV("count", entries.size()) << LOG_KV("validCount", validCount)
                    << LOG_KV("read time(ms)", end - start)
                    << LOG_KV("decode time(ms)", decode - end);
                _callback(nullptr, std::move(entries));
            },
            _keys);
    }
    catch (const std::exception& e)
    {
        STORAGE_TIKV_LOG(WARNING) << LOG_DESC("asyncGetRows failed, need trigger switch")
                                  << LOG_KV("table", _table) << LOG_KV("message", e.what());
        _callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(ReadError, "asyncGetRows failed! ", e),
            std::vector<std::optional<Entry>>());
        triggerSwitch();
    }
}

void TiKVStorage::asyncSetRow(std::string_view _table, std::string_view _key, Entry _entry,
    std::function<void(Error::UniquePtr)> _callback) noexcept
{
    try
    {
        if (!isValid(_table, _key))
        {
            STORAGE_TIKV_LOG(WARNING) << LOG_DESC("asyncGetRow empty tableName or key")
                                      << LOG_KV("table", _table) << LOG_KV("key", _key);
            _callback(BCOS_ERROR_UNIQUE_PTR(TableNotExists, "empty tableName or key"));
            return;
        }
        auto dbKey = toDBKey(_table, _key);
        auto txn = m_cluster->begin();

        if (_entry.status() == Entry::DELETED)
        {
            STORAGE_TIKV_LOG(DEBUG) << LOG_DESC("asyncSetRow delete") << LOG_KV("table", _table)
                                    << LOG_KV("key", _key) << LOG_KV("dbKey", dbKey);
            txn.remove(dbKey);
        }
        else
        {
            if (c_fileLogLevel <= TRACE)
            {
                STORAGE_TIKV_LOG(TRACE)
                    << LOG_DESC("asyncSetRow") << LOG_KV("table", _table) << LOG_KV("key", _key);
            }
            std::string value = std::string(_entry.get());
            txn.put(dbKey, value);
        }
        txn.commit();
        _callback(nullptr);
    }
    catch (const std::exception& e)
    {
        STORAGE_TIKV_LOG(WARNING) << LOG_DESC("asyncSetRow failed") << LOG_KV("table", _table)
                                  << LOG_KV("message", e.what());
        _callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(WriteError, "asyncSetRow failed! ", e));
    }
    m_lastCommitTimestamp = m_cluster->current_timestamp();
}

void TiKVStorage::asyncPrepare(const TwoPCParams& params, const TraverseStorageInterface& storage,
    std::function<void(Error::Ptr, uint64_t startTS, const std::string&)> callback) noexcept
{
    try
    {
        std::unique_lock<std::recursive_mutex> lock(x_committer, std::try_to_lock);
        if (lock.owns_lock())
        {
            STORAGE_TIKV_LOG(INFO)
                << LOG_DESC("asyncPrepare") << LOG_KV("blockNumber", params.number)
                << LOG_KV("primary", params.timestamp > 0 ? "false" : "true");
            auto start = utcTime();
            tbb::spin_mutex writeMutex;
            atomic_bool isTableValid = true;
            std::atomic_uint64_t putCount{0};
            std::atomic_uint64_t deleteCount{0};
            if (m_committer)
            {  // should wait for the previous committer to timeout
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now() - m_committerCreateTime)
                                    .count();
                STORAGE_TIKV_LOG(DEBUG)
                    << "asyncPrepare clean old committer" << LOG_KV("blockNumber", params.number)
                    << LOG_KV("duration(ms)", duration);
                if (duration < m_commitTimeout)
                {
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(m_commitTimeout - duration));
                }
                m_committer->rollback();
                m_committer = nullptr;
            }
            m_committerCreateTime = std::chrono::system_clock::now();
            m_committer = m_cluster->new_optimistic_transaction(MAX_RETRY_LIMIT);
            storage.parallelTraverse(true, [&](const std::string_view& table,
                                               const std::string_view& key, Entry const& entry) {
                if (!isValid(table, key))
                {
                    isTableValid = false;
                    return false;
                }
                auto dbKey = toDBKey(table, key);
                if (entry.status() == Entry::DELETED)
                {
                    tbb::spin_mutex::scoped_lock lock(writeMutex);
                    m_committer->remove(dbKey);
                    ++deleteCount;
                }
                else
                {
                    std::string value = std::string(entry.get());
                    tbb::spin_mutex::scoped_lock lock(writeMutex);
                    m_committer->put(dbKey, value);
                    ++putCount;
                }
                return true;
            });
            if (!isTableValid)
            {
                m_committer->rollback();
                m_committer = nullptr;
                callback(BCOS_ERROR_UNIQUE_PTR(TableNotExists, "empty tableName or key"), 0, "");
                return;
            }
            auto encode = utcTime();
            auto size = putCount + deleteCount;
            if (size == 0)
            {
                m_committer->rollback();
                m_committer = nullptr;
                if (params.timestamp == 0)
                {
                    STORAGE_TIKV_LOG(ERROR) << LOG_DESC("asyncPrepare empty storage")
                                            << LOG_KV("blockNumber", params.number);
                    callback(BCOS_ERROR_UNIQUE_PTR(EmptyStorage, "commit storage is empty"), 0, "");
                }
                else
                {
                    STORAGE_TIKV_LOG(DEBUG) << LOG_DESC("asyncPrepare empty storage")
                                            << LOG_KV("blockNumber", params.number);
                    callback(nullptr, 0, "");
                }
                return;
            }
            auto primaryLock = params.primaryKey;

            if (params.timestamp == 0)
            {
                STORAGE_TIKV_LOG(INFO)
                    << LOG_DESC("asyncPrepare primary") << LOG_KV("blockNumber", params.number);
                auto result = m_committer->prewrite_primary(primaryLock);
                auto write = utcTime();
                m_currentStartTS = result.second;
                lock.unlock();
                callback(nullptr, result.second, result.first);
                STORAGE_TIKV_LOG(INFO)
                    << "asyncPrepare primary finished" << LOG_KV("blockNumber", params.number)
                    << LOG_KV("put", putCount) << LOG_KV("delete", deleteCount)
                    << LOG_KV("size", size) << LOG_KV("primaryLock", toHex(primaryLock))
                    << LOG_KV("primary", toHex(result.first)) << LOG_KV("startTS", result.second)
                    << LOG_KV("encode time(ms)", encode - start)
                    << LOG_KV("prewrite time(ms)", write - encode)
                    << LOG_KV("callback time(ms)", utcTime() - write);
            }
            else
            {
                STORAGE_TIKV_LOG(INFO)
                    << "asyncPrepare secondary" << LOG_KV("blockNumber", params.number)
                    << LOG_KV("put", putCount) << LOG_KV("delete", deleteCount)
                    << LOG_KV("size", size) << LOG_KV("primaryLock", primaryLock)
                    << LOG_KV("startTS", params.timestamp)
                    << LOG_KV("encode time(ms)", encode - start);
                m_currentStartTS = params.timestamp;
                m_committer->prewrite_secondary(primaryLock, m_currentStartTS);
                auto write = utcTime();
                // m_committer = nullptr;
                STORAGE_TIKV_LOG(INFO)
                    << "asyncPrepare secondary finished" << LOG_KV("blockNumber", params.number)
                    << LOG_KV("prewrite time(ms)", write - encode);
                callback(nullptr, 0, primaryLock);
            }
        }
        else
        {
            STORAGE_TIKV_LOG(INFO)
                << "asyncPrepare try_lock failed" << LOG_KV("blockNumber", params.number);
            callback(BCOS_ERROR_UNIQUE_PTR(TryLockFailed, "asyncPrepare try_lock failed"), 0, "");
        }
    }
    catch (const std::exception& e)
    {
        STORAGE_TIKV_LOG(WARNING) << LOG_DESC("asyncPrepare failed")
                                  << LOG_KV("blockNumber", params.number)
                                  << LOG_KV("message", e.what());
        callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(WriteError, "asyncPrepare failed! ", e), 0, "");
    }
}

void TiKVStorage::asyncCommit(
    const TwoPCParams& params, std::function<void(Error::Ptr, uint64_t)> callback) noexcept
{
    try
    {
        std::unique_lock<std::recursive_mutex> lock(x_committer, std::try_to_lock);
        if (lock.owns_lock())
        {
            STORAGE_TIKV_LOG(INFO)
                << LOG_DESC("asyncCommit") << LOG_KV("blockNumber", params.number)
                << LOG_KV("timestamp", params.timestamp)
                << LOG_KV("primary", params.timestamp > 0 ? "false" : "true");
            auto start = utcTime();
            uint64_t ts = 0;
            if (m_committer)
            {
                if (params.timestamp > 0)
                {
                    m_committer->commit_secondary(params.timestamp);
                }
                else
                {
                    ts = m_committer->commit_primary();
                    m_committer->commit_secondary(ts);
                }
                m_committer = nullptr;
            }
            auto end = utcTime();
            STORAGE_TIKV_LOG(INFO)
                << LOG_DESC("asyncCommit finished") << LOG_KV("blockNumber", params.number)
                << LOG_KV("commitTS", params.timestamp) << LOG_KV("primaryCommitTS", ts)
                << LOG_KV("time(ms)", end - start);
            lock.unlock();
            m_lastCommitTimestamp = m_cluster->current_timestamp();
            callback(nullptr, ts);
        }
        else
        {
            STORAGE_TIKV_LOG(INFO)
                << "asyncCommit try_lock failed" << LOG_KV("blockNumber", params.number);
            callback(BCOS_ERROR_UNIQUE_PTR(TryLockFailed, "asyncPrepare try_lock failed"), 0);
        }
    }
    catch (const std::exception& e)
    {
        STORAGE_TIKV_LOG(WARNING) << LOG_DESC("asyncCommit failed")
                                  << LOG_KV("blockNumber", params.number)
                                  << LOG_KV("commitTS", params.timestamp)
                                  << LOG_KV("message", e.what());
        callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(WriteError, "asyncCommit failed! ", e), 0);
    }
}

void TiKVStorage::asyncRollback(
    const TwoPCParams& params, std::function<void(Error::Ptr)> callback) noexcept
{
    try
    {
        std::unique_lock<std::recursive_mutex> lock(x_committer, std::try_to_lock);
        if (lock.owns_lock())
        {
            if (m_currentStartTS != params.timestamp)
            {
                STORAGE_TIKV_LOG(INFO)
                    << "asyncRollback wrong timestamp" << LOG_KV("blockNumber", params.number)
                    << LOG_KV("expect", params.timestamp) << LOG_KV("current", m_currentStartTS);
                callback(BCOS_ERROR_UNIQUE_PTR(
                    TimestampMismatch, "asyncRollback failed for TimestampMismatch"));
                return;
            }
            STORAGE_TIKV_LOG(INFO)
                << LOG_DESC("asyncRollback") << LOG_KV("blockNumber", params.number)
                << LOG_KV("timestamp", params.timestamp);
            RecursiveGuard guard(x_committer);
            auto start = utcTime();
            if (m_committer)
            {
                m_committer->rollback();
                m_committer = nullptr;
            }
            auto end = utcTime();
            lock.unlock();
            callback(nullptr);
            STORAGE_TIKV_LOG(INFO)
                << LOG_DESC("asyncRollback finished") << LOG_KV("blockNumber", params.number)
                << LOG_KV("startTS", params.timestamp) << LOG_KV("time(ms)", end - start)
                << LOG_KV("callback time(ms)", utcTime() - end);
        }
        else
        {
            STORAGE_TIKV_LOG(INFO)
                << "asyncRollback try_lock failed" << LOG_KV("blockNumber", params.number);
            callback(BCOS_ERROR_UNIQUE_PTR(TryLockFailed, "asyncPrepare try_lock failed"));
        }
    }
    catch (const std::exception& e)
    {
        STORAGE_TIKV_LOG(WARNING) << LOG_DESC("asyncRollback failed")
                                  << LOG_KV("blockNumber", params.number)
                                  << LOG_KV("message", e.what());
        callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(WriteError, "asyncRollback failed! ", e));
    }
}

bcos::Error::Ptr TiKVStorage::setRows(std::string_view table, std::vector<std::string_view> keys,
    std::vector<std::string_view> values) noexcept
{
    try
    {
        if (table.empty())
        {
            STORAGE_TIKV_LOG(WARNING)
                << LOG_DESC("setRows empty tableName") << LOG_KV("table", table);
            return BCOS_ERROR_PTR(TableNotExists, "empty tableName");
        }
        if (keys.size() != values.size())
        {
            STORAGE_TIKV_LOG(WARNING)
                << LOG_DESC("setRows values size mismatch keys size") << LOG_KV("table", table)
                << LOG_KV("keys", keys.size()) << LOG_KV("values", values.size());
            return BCOS_ERROR_PTR(TableNotExists, "setRows values size mismatch keys size");
        }
        if (keys.empty())
        {
            STORAGE_TIKV_LOG(WARNING) << LOG_DESC("setRows empty keys") << LOG_KV("table", table);
            return nullptr;
        }
        std::vector<std::string> realKeys(keys.size());
        tbb::parallel_for(tbb::blocked_range<size_t>(0, keys.size()),
            [&](const tbb::blocked_range<size_t>& range) {
                for (size_t i = range.begin(); i != range.end(); ++i)
                {
                    realKeys[i] = toDBKey(table, keys[i]);
                }
            });
        auto txn = m_cluster->begin();
        for (size_t i = 0; i < values.size(); ++i)
        {
            txn.put(std::string(realKeys[i]), std::string(values[i]));
        }
        txn.commit();
    }
    catch (std::exception& e)
    {
        STORAGE_TIKV_LOG(WARNING) << LOG_DESC("setRows failed") << LOG_KV("what", e.what());
        m_lastCommitTimestamp = m_cluster->current_timestamp();
        return BCOS_ERROR_WITH_PREV_PTR(WriteError, "setRows failed! ", e);
    }
    m_lastCommitTimestamp = m_cluster->current_timestamp();
    return nullptr;
}

void TiKVStorage::triggerSwitch()
{
    if (f_onNeedSwitchEvent)
    {
        STORAGE_TIKV_LOG(WARNING) << LOG_DESC("Trigger switch");
        f_onNeedSwitchEvent();
    }
}

void TiKVStorage::reset()
{
    m_lastCommitTimestamp = m_cluster->current_timestamp();
}
