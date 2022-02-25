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
 */
/**
 * @brief the storage implement in memory
 * @file MemoryStorage.h
 * @author: xingqiangbai
 * @date: 2021-06-09
 */

#pragma once

#include "bcos-framework/interfaces/storage/StorageInterface.h"
#include <mutex>
#include <string>

namespace bcos
{
namespace storage
{
class MemoryStorage : public StorageInterface
{
public:
    typedef std::shared_ptr<MemoryStorage> Ptr;
    MemoryStorage() { data[storage::SYS_TABLE] = std::map<std::string, Entry::Ptr>(); }
    virtual ~MemoryStorage() = default;

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
        if (data.count(_tableInfo->name))
        {
            if (data[_tableInfo->name].count(std::string(_key)))
            {
                return data[_tableInfo->name][std::string(_key)];
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
            auto stateData = stateTableFactory->exportData();
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
        auto keyList = getPrimaryKeys(_tableInfo, _condition);
        boost::this_thread::sleep_for(boost::chrono::milliseconds(SLEEP_MILLI_SECONDS));
        auto error = std::make_shared<Error>(0, "");
        _callback(error, keyList);
    }

    void asyncGetRow(const TableInfo::Ptr& _tableInfo, const std::string_view& _key,
        std::function<void(const Error::Ptr&, const Entry::Ptr&)> _callback) override
    {
        auto entry = getRow(_tableInfo, _key);
        boost::this_thread::sleep_for(boost::chrono::milliseconds(SLEEP_MILLI_SECONDS));
        auto error = std::make_shared<Error>(0, "");
        _callback(error, entry);
    }
    void asyncGetRows(const std::shared_ptr<TableInfo>& _tableInfo,
        const std::shared_ptr<std::vector<std::string>>& _keyList,
        std::function<void(const Error::Ptr&, const std::map<std::string, Entry::Ptr>&)> _callback)
        override
    {
        auto rowMap = getRows(_tableInfo, *_keyList);
        boost::this_thread::sleep_for(boost::chrono::milliseconds(SLEEP_MILLI_SECONDS));
        auto error = std::make_shared<Error>(0, "");
        _callback(error, rowMap);
    }

    void asyncCommitBlock(protocol::BlockNumber _number,
        const std::shared_ptr<std::vector<std::shared_ptr<TableInfo>>>& _tableInfo,
        const std::shared_ptr<std::vector<std::shared_ptr<std::map<std::string, Entry::Ptr>>>>&
            _tableMap,
        std::function<void(const Error::Ptr&, size_t)> _callback) override
    {
        auto retPair = commitBlock(_number, *_tableInfo, *_tableMap);
        boost::this_thread::sleep_for(boost::chrono::milliseconds(SLEEP_MILLI_SECONDS));
        auto error = std::make_shared<Error>(0, "");
        _callback(error, retPair.first);
    }

    // cache StateStorage
    void asyncAddStateCache(protocol::BlockNumber _number,
        const std::shared_ptr<TableFactoryInterface>& _table,
        std::function<void(const Error::Ptr&)> _callback) override
    {
        boost::this_thread::sleep_for(boost::chrono::milliseconds(SLEEP_MILLI_SECONDS));
        addStateCache(_number, _table);
        _callback(nullptr);
    }
    void asyncDropStateCache(protocol::BlockNumber, std::function<void(const Error::Ptr&)>) override
    {}
    void asyncGetStateCache(protocol::BlockNumber _blockNumber,
        std::function<void(const Error::Ptr&, const std::shared_ptr<TableFactoryInterface>&)>
            _callback) override
    {
        auto tableFactory = getStateCache(_blockNumber);
        boost::this_thread::sleep_for(boost::chrono::milliseconds(SLEEP_MILLI_SECONDS));
        auto error = std::make_shared<Error>(0, "");
        _callback(error, tableFactory);
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

    void asyncPut(const std::string_view& _columnFamily, const std::string_view& _key,
        const std::string_view& _value, std::function<void(const Error::Ptr&)> _callback) override
    {
        auto key = getKey(_columnFamily, _key);
        m_key2Data[key] = _value;
        _callback(nullptr);
    }

    void asyncRemove(const std::string_view& _columnFamily, const std::string_view& _key,
        std::function<void(const Error::Ptr&)> _callback) override
    {
        auto key = getKey(_columnFamily, _key);
        if (!m_key2Data.count(key))
        {
            _callback(std::make_shared<Error>(StorageErrorCode::NotFound, "key NotFound"));
            return;
        }
        m_key2Data.erase(key);
        _callback(nullptr);
    }

    void asyncGet(const std::string_view& _columnFamily, const std::string_view& _key,
        std::function<void(const Error::Ptr&, const std::string& value)> _callback) override
    {
        auto key = getKey(_columnFamily, _key);
        if (!m_key2Data.count(key))
        {
            _callback(std::make_shared<Error>(StorageErrorCode::NotFound, "key NotFound"), "");
            return;
        }
        _callback(nullptr, m_key2Data[key]);
    }

    void asyncGetBatch(const std::string_view& _columnFamily,
        const std::shared_ptr<std::vector<std::string>>& _keys,
        std::function<void(const Error::Ptr&, const std::shared_ptr<std::vector<std::string>>&)>
            callback) override
    {
        auto result = std::make_shared<std::vector<std::string>>();
        for (auto const& _key : *_keys)
        {
            auto key = getKey(_columnFamily, _key);
            if (!m_key2Data.count(key))
            {
                result->push_back("");
                continue;
            }
            result->push_back(m_key2Data[key]);
        }
        callback(nullptr, result);
    }


protected:
    std::string getKey(const std::string_view& _columnFamily, const std::string_view& _key)
    {
        std::string columnFamily(_columnFamily.data(), _columnFamily.size());
        std::string key(_key.data(), _key.size());
        return columnFamily + "_" + key;
    }

    std::map<std::string, std::string> m_key2Data;

private:
    std::map<std::string, std::map<std::string, Entry::Ptr>> data;
    mutable std::mutex m_mutex;
    std::map<protocol::BlockNumber, TableFactoryInterface::Ptr> m_number2TableFactory;
};

}  // namespace storage

}  // namespace bcos
