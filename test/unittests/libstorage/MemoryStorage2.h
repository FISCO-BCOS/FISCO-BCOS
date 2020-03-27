/**
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 *
 * @brief
 *
 * @file ExecuteVMTest.cpp
 * @author: xingqiangbai
 * @date 2018-10-25
 */

#pragma once

#include "libstorage/Storage.h"
#include <tbb/mutex.h>

namespace dev
{
namespace storage
{
class MemoryStorage2 : public Storage
{
public:
    typedef std::shared_ptr<MemoryStorage2> Ptr;

    virtual ~MemoryStorage2(){};

    Entries::Ptr select(int64_t, TableInfo::Ptr tableInfo, const std::string& key,
        Condition::Ptr condition) override
    {
        auto tableKey = tableInfo->name + key;
        tbb::mutex::scoped_lock lock(m_mutex);

        auto entries = std::make_shared<Entries>();
        auto it = key2Entries.find(tableKey);
        if (it != key2Entries.end())
        {
            for (auto const& item : it->second)
            {
                auto entry = item.second;
                if (entry->getStatus() == Entry::Status::NORMAL &&
                    (!condition || condition->process(entry)))
                {
                    entry->setDirty(false);
                    entries->addEntry(entry);
                }
            }
        }

        return entries;
    }
    size_t commit(int64_t, const std::vector<TableData::Ptr>& datas) override
    {
        for (auto tableData : datas)
        {
            processEntries(tableData->info->name, tableData->info->key, tableData->dirtyEntries);
            processEntries(tableData->info->name, tableData->info->key, tableData->newEntries);
        }
        return datas.size();
    }
    bool onlyCommitDirty() override { return false; }
    void processEntries(const std::string& tableName, const std::string& key, Entries::Ptr entries)
    {
        for (size_t i = 0; i < entries->size(); ++i)
        {
            auto entry = entries->get(i);
            if (entry->deleted())
            {
                continue;
            }
            auto tableKey = tableName + entry->getField(key);
            tbb::mutex::scoped_lock lock(m_mutex);
            key2Entries[tableKey][entry->getID()] = entry;
        }
    }

private:
    std::map<std::string, std::map<uint64_t, Entry::Ptr>> key2Entries;
    tbb::mutex m_mutex;
};

class MemoryStorageFactory : public StorageFactory
{
public:
    MemoryStorageFactory() {}
    virtual ~MemoryStorageFactory() {}
    Storage::Ptr getStorage(const std::string& _dbName, bool = false) override
    {
        RecursiveGuard l(x_cache);
        auto it = m_cache.find(_dbName);
        if (it == m_cache.end())
        {
            m_cache[_dbName] = std::make_shared<MemoryStorage2>();
            return m_cache[_dbName];
        }
        return it->second;
    }

private:
    std::recursive_mutex x_cache;
    std::map<std::string, Storage::Ptr> m_cache;
};
}  // namespace storage

}  // namespace dev
