/**
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
 * @file MockStorage.h
 * @author: kyonRay
 * @date 2021-05-06
 */

#pragma once

#include "bcos-framework/interfaces/storage/Common.h"
#include "bcos-framework/interfaces/storage/StorageInterface.h"
#include "bcos-framework/interfaces/storage/Table.h"
#include "bcos-ledger/libledger/utilities/Common.h"
#include <bcos-utilities/ThreadPool.h>
#define SLEEP_MILLI_SECONDS 10

using namespace bcos::storage;
using namespace bcos::ledger;
namespace bcos::test
{
class MockStorage : public StorageInterface
{
public:
    MockStorage()
    {
        data[storage::SYS_TABLE] = std::map<std::string, Entry::Ptr>();
        m_threadPool = std::make_shared<bcos::ThreadPool>("mockStorage", 4);
    }
    virtual ~MockStorage() { m_threadPool->stop(); }

    std::vector<std::string> getPrimaryKeys(const std::shared_ptr<TableInfo>& _tableInfo,
        const Condition::Ptr& _condition) const override
    {
        std::vector<std::string> ret;
        std::lock_guard<std::mutex> lock(m_mutex);
        if (data.count(_tableInfo->name))
        {
            for (auto& kv : data.at(_tableInfo->name))
            {
                if (!_condition || _condition->isValid(kv.first))
                {
                    ret.emplace_back(kv.first);
                }
            }
        }
        return ret;
    }
    std::shared_ptr<Entry> getRow(
        const std::shared_ptr<TableInfo>& _tableInfo, const std::string_view& _key) override
    {
        std::shared_ptr<Entry> ret = nullptr;
        std::lock_guard<std::mutex> lock(m_mutex);
        if (data.find(_tableInfo->name) != data.end())
        {
            if (data[_tableInfo->name].find(std::string(_key)) != data[_tableInfo->name].end())
            {
                return data[_tableInfo->name][std::string(_key)];
            }
            else
            {
                LEDGER_LOG(TRACE) << LOG_BADGE("getRow") << LOG_DESC("can't find key")
                                  << LOG_KV("key", _key);
            }
        }
        return ret;
    }
    std::map<std::string, std::shared_ptr<Entry>> getRows(
        const std::shared_ptr<TableInfo>& _tableInfo,
        const std::vector<std::string>& _keys) override
    {
        std::map<std::string, std::shared_ptr<Entry>> ret;
        std::lock_guard<std::mutex> lock(m_mutex);
        if (data.count(_tableInfo->name))
        {
            for (auto& key : _keys)
            {
                if (data[_tableInfo->name].count(std::string(key)))
                {
                    ret[key] = data[_tableInfo->name][key];
                }
            }
        }
        return ret;
    }
    std::pair<size_t, Error::Ptr> commitBlock(protocol::BlockNumber _number,
        const std::vector<std::shared_ptr<TableInfo>>& _tableInfos,
        const std::vector<std::shared_ptr<std::map<std::string, Entry::Ptr>>>& _tableDatas) override
    {
        size_t total = 0;
        if (_tableInfos.size() != _tableDatas.size())
        {
            auto error = std::make_shared<Error>(-1, "");
            return {0, error};
        }
        std::shared_ptr<TableFactoryInterface> stateTableFactory = nullptr;
        if (_number != 0)
        {
            if (m_number2TableFactory.count(_number))
            {
                stateTableFactory = m_number2TableFactory[_number];
            }
            else
            {
                return {0, std::make_shared<Error>(StorageErrorCode::StateCacheNotFound,
                               std::to_string(_number) + "state cache not found")};
            }
            auto stateData = stateTableFactory->exportData(_number);
            stateData.first.insert(stateData.first.end(), _tableInfos.begin(), _tableInfos.end());
            stateData.second.insert(stateData.second.end(), _tableDatas.begin(), _tableDatas.end());
            std::lock_guard<std::mutex> lock(m_mutex);
            for (size_t i = 0; i < stateData.first.size(); ++i)
            {
                for (auto& item : *stateData.second[i])
                {
                    if (item.second->getStatus() == Entry::Status::NORMAL)
                    {
                        data[stateData.first[i]->name][item.first] = item.second;
                        ++total;
                    }
                }
            }
        }
        else
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (size_t i = 0; i < _tableInfos.size(); ++i)
            {
                for (auto& item : *_tableDatas[i])
                {
                    if (item.second->getStatus() == Entry::Status::NORMAL)
                    {
                        data[_tableInfos[i]->name][item.first] = item.second;
                        ++total;
                    }
                }
            }
        }
        return {total, nullptr};
    }

    void asyncGetPrimaryKeys(const std::shared_ptr<TableInfo>& _tableInfo,
        const Condition::Ptr& _condition,
        std::function<void(const Error::Ptr&, const std::vector<std::string>&)> _callback) override
    {
        auto self =
            std::weak_ptr<MockStorage>(std::dynamic_pointer_cast<MockStorage>(shared_from_this()));
        m_threadPool->enqueue([_tableInfo, _condition, _callback, self]() {
            auto storage = self.lock();
            if (storage)
            {
                time_t t = time(0);
                auto keyList = storage->getPrimaryKeys(_tableInfo, _condition);
                boost::this_thread::sleep_for(boost::chrono::milliseconds(SLEEP_MILLI_SECONDS));
                auto success = std::make_shared<Error>(0, "");
                LEDGER_LOG(TRACE) << LOG_BADGE("asyncGetPrimaryKeys")
                                  << LOG_DESC("storage getKeys finish")
                                  << LOG_KV("tableName", _tableInfo->name)
                                  << LOG_KV("exec_time", time(0) - t);
                t = time(0);
                _callback(success, keyList);
                LEDGER_LOG(TRACE) << LOG_BADGE("asyncGetPrimaryKeys")
                                  << LOG_DESC("storage callback")
                                  << LOG_KV("tableName", _tableInfo->name)
                                  << LOG_KV("callback_time", time(0) - t);
            }
            else
            {
                _callback(std::make_shared<Error>(-1, ""), {});
            }
        });
    }

    void asyncGetRow(const TableInfo::Ptr& _tableInfo, const std::string_view& _key,
        std::function<void(const Error::Ptr&, const Entry::Ptr&)> _callback) override
    {
        auto key = std::string(_key);
        auto self =
            std::weak_ptr<MockStorage>(std::dynamic_pointer_cast<MockStorage>(shared_from_this()));
        m_threadPool->enqueue([_tableInfo, key, _callback, self]() {
            auto storage = self.lock();
            if (storage)
            {
                time_t t = time(0);
                auto entry = storage->getRow(_tableInfo, key);
                boost::this_thread::sleep_for(boost::chrono::milliseconds(SLEEP_MILLI_SECONDS));
                LEDGER_LOG(TRACE) << LOG_BADGE("asyncGetRow") << LOG_DESC("storage getRow finish")
                                  << LOG_KV("tableName", _tableInfo->name)
                                  << LOG_KV("key", std::string(key))
                                  << LOG_KV("exec_time", time(0) - t);
                t = time(0);
                _callback(nullptr, entry);
                LEDGER_LOG(TRACE) << LOG_BADGE("asyncGetRow") << LOG_DESC("storage callback")
                                  << LOG_KV("tableName", _tableInfo->name)
                                  << LOG_KV("key", std::string(key))
                                  << LOG_KV("callback_time", time(0) - t);
            }
            else
            {
                _callback(std::make_shared<Error>(-1, ""), nullptr);
            }
        });
    }
    void asyncGetRows(const std::shared_ptr<TableInfo>& _tableInfo,
        const std::shared_ptr<std::vector<std::string>>& _keyList,
        std::function<void(const Error::Ptr&, const std::map<std::string, Entry::Ptr>&)> _callback)
        override
    {
        auto self =
            std::weak_ptr<MockStorage>(std::dynamic_pointer_cast<MockStorage>(shared_from_this()));
        m_threadPool->enqueue([_tableInfo, _keyList, _callback, self]() {
            auto storage = self.lock();
            if (storage)
            {
                time_t t = time(0);
                auto rowMap = storage->getRows(_tableInfo, *_keyList);
                boost::this_thread::sleep_for(boost::chrono::milliseconds(SLEEP_MILLI_SECONDS));
                LEDGER_LOG(TRACE) << LOG_BADGE("asyncGetRows") << LOG_DESC("storage getRows finish")
                                  << LOG_KV("tableName", _tableInfo->name)
                                  << LOG_KV("exec_time", time(0) - t);
                t = time(0);
                _callback(nullptr, rowMap);
                LEDGER_LOG(TRACE) << LOG_BADGE("asyncGetRows") << LOG_DESC("storage callback")
                                  << LOG_KV("tableName", _tableInfo->name)
                                  << LOG_KV("callback_time", time(0) - t);
            }
            else
            {
                _callback(std::make_shared<Error>(-1, ""), {});
            }
        });
    }

    void asyncCommitBlock(protocol::BlockNumber _number,
        const std::shared_ptr<std::vector<std::shared_ptr<TableInfo>>>& _tableInfo,
        const std::shared_ptr<std::vector<std::shared_ptr<std::map<std::string, Entry::Ptr>>>>&
            _tableMap,
        std::function<void(const Error::Ptr&, size_t)> _callback) override
    {
        auto self =
            std::weak_ptr<MockStorage>(std::dynamic_pointer_cast<MockStorage>(shared_from_this()));
        m_threadPool->enqueue([_number, _tableInfo, _tableMap, _callback, self]() {
            auto storage = self.lock();
            if (storage)
            {
                time_t t = time(0);
                auto retPair = storage->commitBlock(_number, *_tableInfo, *_tableMap);
                boost::this_thread::sleep_for(boost::chrono::milliseconds(SLEEP_MILLI_SECONDS));
                auto error = std::make_shared<Error>(0, "");
                LEDGER_LOG(TRACE) << LOG_BADGE("asyncCommitBlock")
                                  << LOG_DESC("storage commit finish") << LOG_KV("number", _number)
                                  << LOG_KV("exec_time", time(0) - t);
                t = time(0);
                _callback(error, retPair.first);
                LEDGER_LOG(TRACE) << LOG_BADGE("asyncCommitBlock") << LOG_DESC("storage callback")
                                  << LOG_KV("callback_time", time(0) - t);
            }
            else
            {
                _callback(std::make_shared<Error>(-1, ""), -1);
            }
        });
    }

    // cache TableFactory
    void asyncAddStateCache(protocol::BlockNumber _number,
        const std::shared_ptr<TableFactoryInterface>& _table,
        std::function<void(const Error::Ptr&)> _callback) override
    {
        auto self =
            std::weak_ptr<MockStorage>(std::dynamic_pointer_cast<MockStorage>(shared_from_this()));
        m_threadPool->enqueue([_number, _table, _callback, self]() {
            auto storage = self.lock();
            if (storage)
            {
                boost::this_thread::sleep_for(boost::chrono::milliseconds(SLEEP_MILLI_SECONDS));
                storage->addStateCache(_number, _table);
                _callback(nullptr);
            }
            else
            {
                _callback(std::make_shared<Error>(-1, ""));
            }
        });
    }
    void asyncDropStateCache(protocol::BlockNumber, std::function<void(const Error::Ptr&)>) override
    {}
    void asyncGetStateCache(protocol::BlockNumber _blockNumber,
        std::function<void(const Error::Ptr&, const std::shared_ptr<TableFactoryInterface>&)>
            _callback) override
    {
        auto self =
            std::weak_ptr<MockStorage>(std::dynamic_pointer_cast<MockStorage>(shared_from_this()));
        m_threadPool->enqueue([_blockNumber, _callback, self]() {
            auto storage = self.lock();
            if (storage)
            {
                auto tableFactory = storage->getStateCache(_blockNumber);
                boost::this_thread::sleep_for(boost::chrono::milliseconds(SLEEP_MILLI_SECONDS));
                auto error = std::make_shared<Error>(0, "");
                _callback(error, tableFactory);
            }
            else
            {
                _callback(std::make_shared<Error>(-1, ""), nullptr);
            }
        });
    }

    std::shared_ptr<TableFactoryInterface> getStateCache(
        protocol::BlockNumber _blockNumber) override
    {
        if (m_number2TableFactory.count(_blockNumber))
        {
            return m_number2TableFactory[_blockNumber];
        }
        return nullptr;
    }

    void dropStateCache(protocol::BlockNumber) override {}
    void addStateCache(protocol::BlockNumber _blockNumber,
        const std::shared_ptr<TableFactoryInterface>& _tableFactory) override
    {
        m_number2TableFactory[_blockNumber] = _tableFactory;
    }
    // KV store in split database, used to store data off-chain
    Error::Ptr put(
        const std::string_view&, const std::string_view&, const std::string_view&) override
    {
        return nullptr;
    }
    std::pair<std::string, Error::Ptr> get(
        const std::string_view&, const std::string_view&) override
    {
        return {"", nullptr};
    }
    Error::Ptr remove(const std::string_view&, const std::string_view&) override { return nullptr; }
    void asyncRemove(const std::string_view&, const std::string_view&,
        std::function<void(const Error::Ptr&)>) override
    {}
    void asyncPut(const std::string_view&, const std::string_view&, const std::string_view&,
        std::function<void(const Error::Ptr&)>) override
    {}
    void asyncGet(const std::string_view&, const std::string_view&,
        std::function<void(const Error::Ptr&, const std::string& value)>) override
    {}
    void asyncGetBatch(const std::string_view&, const std::shared_ptr<std::vector<std::string>>&,
        std::function<void(const Error::Ptr&, const std::shared_ptr<std::vector<std::string>>&)>)
        override
    {}

private:
    bcos::ThreadPool::Ptr m_threadPool = nullptr;
    std::map<std::string, std::map<std::string, Entry::Ptr>> data;
    mutable std::mutex m_mutex;
    std::map<protocol::BlockNumber, TableFactoryInterface::Ptr> m_number2TableFactory;
};

class MockErrorStorage : public MockStorage
{
public:
    MockErrorStorage() : MockStorage() {}
    std::vector<std::string> getPrimaryKeys(const std::shared_ptr<TableInfo>& _tableInfo,
        const Condition::Ptr& _condition) const override
    {
        return MockStorage::getPrimaryKeys(_tableInfo, _condition);
    }
    std::shared_ptr<Entry> getRow(
        const std::shared_ptr<TableInfo>& _tableInfo, const std::string_view& _key) override
    {
        return MockStorage::getRow(_tableInfo, _key);
    }
    std::map<std::string, std::shared_ptr<Entry>> getRows(
        const std::shared_ptr<TableInfo>& _tableInfo,
        const std::vector<std::string>& _keys) override
    {
        return MockStorage::getRows(_tableInfo, _keys);
    }
    std::pair<size_t, Error::Ptr> commitBlock(protocol::BlockNumber number,
        const std::vector<std::shared_ptr<TableInfo>>& _tableInfos,
        const std::vector<std::shared_ptr<std::map<std::string, Entry::Ptr>>>& _tableDatas) override
    {
        return MockStorage::commitBlock(number, _tableInfos, _tableDatas);
    }
    std::shared_ptr<TableFactoryInterface> getStateCache(
        protocol::BlockNumber _blockNumber) override
    {
        return MockStorage::getStateCache(_blockNumber);
    }
    void addStateCache(protocol::BlockNumber _blockNumber,
        const std::shared_ptr<TableFactoryInterface>& _tableFactory) override
    {
        MockStorage::addStateCache(_blockNumber, _tableFactory);
    }
    void asyncGetPrimaryKeys(const std::shared_ptr<TableInfo>& _tableInfo,
        const Condition::Ptr& _condition,
        std::function<void(const Error::Ptr&, const std::vector<std::string>&)> _callback) override
    {
        MockStorage::asyncGetPrimaryKeys(_tableInfo, _condition, _callback);
    }
    void asyncGetRow(const TableInfo::Ptr& _tableInfo, const std::string_view& _key,
        std::function<void(const Error::Ptr&, const Entry::Ptr&)> _callback) override
    {
        MockStorage::asyncGetRow(_tableInfo, _key, _callback);
    }
    void asyncGetRows(const std::shared_ptr<TableInfo>& _tableInfo,
        const std::shared_ptr<std::vector<std::string>>& _keyList,
        std::function<void(const Error::Ptr&, const std::map<std::string, Entry::Ptr>&)> _callback)
        override
    {
        MockStorage::asyncGetRows(_tableInfo, _keyList, _callback);
    }
    void asyncCommitBlock(protocol::BlockNumber,
        const std::shared_ptr<std::vector<std::shared_ptr<TableInfo>>>&,
        const std::shared_ptr<std::vector<std::shared_ptr<std::map<std::string, Entry::Ptr>>>>&,
        std::function<void(const Error::Ptr&, size_t)> _callback) override
    {
        _callback(std::make_shared<Error>(-1, ""), 0);
    }
    void asyncAddStateCache(protocol::BlockNumber, const std::shared_ptr<TableFactoryInterface>&,
        std::function<void(const Error::Ptr&)> _callback) override
    {
        _callback(std::make_shared<Error>(-1, ""));
    }
    void asyncGetStateCache(protocol::BlockNumber _number,
        std::function<void(const Error::Ptr&, const std::shared_ptr<TableFactoryInterface>&)>
            _callback) override
    {
        // random get success
        auto rand = random();
        if (rand & 1)
        {
            if (rand & 3)
            {
                _callback(nullptr, nullptr);
            }
            else
            {
                _callback(std::make_shared<Error>(-1, ""), nullptr);
            }
        }
        else
        {
            MockStorage::asyncGetStateCache(_number, _callback);
        }
    }
};
}  // namespace bcos::test