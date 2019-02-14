/*
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
 */
/** @file MemoryTableFactory.h
 *  @author ancelmo
 *  @date 20180921
 */
#pragma once

#include "Common.h"
#include "MemoryTable.h"
#include "Storage.h"
#include "Table.h"
#include "TablePrecompiled.h"
#include <libdevcore/easylog.h>
#include <boost/algorithm/string.hpp>
#include <memory>
#include <type_traits>

namespace dev
{
namespace blockverifier
{
class ExecutiveContext;
}
namespace storage
{
class MemoryTableFactory
{
public:
    typedef std::shared_ptr<MemoryTableFactory> Ptr;
    MemoryTableFactory();
    virtual ~MemoryTableFactory() {}

    template <typename Mode = Serial>
    typename Table<Mode>::Ptr openTable(const std::string& tableName, bool authorityFlag = true)
    {
        auto it = m_name2Table.find(tableName);
        if (it != m_name2Table.end())
        {
            return std::dynamic_pointer_cast<Table<Mode>>(it->second);
        }
        auto tableInfo = std::make_shared<storage::TableInfo>();

        if (m_sysTables.end() != find(m_sysTables.begin(), m_sysTables.end(), tableName))
        {
            tableInfo = getSysTableInfo(tableName);
        }
        else
        {
            auto tempSysTable = openTable(SYS_TABLES);
            auto tableEntries = tempSysTable->select(tableName, tempSysTable->newCondition());
            if (tableEntries->size() == 0u)
            {
                /*
                STORAGE_LOG(DEBUG) << LOG_BADGE("MemoryTableFactory")
                                   << LOG_DESC("table doesn't exist in _sys_tables_")
                                   << LOG_KV("table name", tableName);
                                   */
                return nullptr;
            }
            auto entry = tableEntries->get(0);
            tableInfo->name = tableName;
            tableInfo->key = entry->getField("key_field");
            std::string valueFields = entry->getField("value_field");
            boost::split(tableInfo->fields, valueFields, boost::is_any_of(","));
        }
        tableInfo->fields.emplace_back(STATUS);
        tableInfo->fields.emplace_back(tableInfo->key);
        tableInfo->fields.emplace_back("_hash_");
        tableInfo->fields.emplace_back("_num_");

        typename MemoryTable<Mode>::Ptr memoryTable = std::make_shared<MemoryTable<Mode>>();
        memoryTable->setStateStorage(m_stateStorage);
        memoryTable->setBlockHash(m_blockHash);
        memoryTable->setBlockNum(m_blockNum);

        // authority flag
        if (authorityFlag)
        {
            // set authorized address to memoryTable
            if (tableName != std::string(SYS_ACCESS_TABLE))
            {
                setAuthorizedAddress(tableInfo);
            }
            else
            {
                memoryTable->setTableInfo(tableInfo);
                auto tableEntries =
                    memoryTable->select(tableInfo->name, memoryTable->newCondition());
                for (size_t i = 0; i < tableEntries->size(); ++i)
                {
                    auto entry = tableEntries->get(i);
                    if (std::stoi(entry->getField("enable_num")) <= m_blockNum)
                    {
                        tableInfo->authorizedAddress.emplace_back(entry->getField("address"));
                    }
                }
            }
        }

        memoryTable->setTableInfo(tableInfo);
        memoryTable->setRecorder(
            [&](typename Table<Mode>::Ptr _table, Change::Kind _kind, std::string const& _key,
                std::vector<Change::Record>& _records) {
                m_changeLog.emplace_back(_table, _kind, _key, _records);
            });

        memoryTable->init(tableName);
        m_name2Table.insert({tableName, memoryTable});
        return memoryTable;
    }

    template <typename Mode = Serial>
    typename Table<Mode>::Ptr createTable(const std::string& tableName, const std::string& keyField,
        const std::string& valueField, bool authorigytFlag, Address const& _origin = Address())
    {
        auto sysTable = openTable(SYS_TABLES, authorigytFlag);

        // To make sure the table exists
        auto tableEntries = sysTable->select(tableName, sysTable->newCondition());
        if (tableEntries->size() != 0)
        {
            STORAGE_LOG(ERROR) << LOG_BADGE("MemoryTableFactory")
                               << LOG_DESC("table already exist in _sys_tables_")
                               << LOG_KV("table name", tableName);
            createTableCode = 0;
            return nullptr;
        }
        // Write table entry
        auto tableEntry = sysTable->newEntry();
        tableEntry->setField("table_name", tableName);
        tableEntry->setField("key_field", keyField);
        tableEntry->setField("value_field", valueField);
        createTableCode =
            sysTable->insert(tableName, tableEntry, std::make_shared<AccessOptions>(_origin));
        if (createTableCode == -1)
        {
            STORAGE_LOG(WARNING) << LOG_BADGE("MemoryTableFactory")
                                 << LOG_DESC("create table non-authorized")
                                 << LOG_KV("origin", _origin.hex())
                                 << LOG_KV("table name", tableName);
            return nullptr;
        }
        return openTable(tableName);
    }

    virtual Storage::Ptr stateStorage() { return m_stateStorage; }
    virtual void setStateStorage(Storage::Ptr stateStorage) { m_stateStorage = stateStorage; }

    void setBlockHash(h256 blockHash);
    void setBlockNum(int64_t blockNum);

    h256 hash();
    size_t savepoint() const { return m_changeLog.size(); };
    void rollback(size_t _savepoint);
    void commit();
    void commitDB(h256 const& _blockHash, int64_t _blockNumber);
    int getCreateTableCode() { return createTableCode; }

private:
    storage::TableInfo::Ptr getSysTableInfo(const std::string& tableName);
    void setAuthorizedAddress(storage::TableInfo::Ptr _tableInfo);
    Storage::Ptr m_stateStorage;
    h256 m_blockHash;
    int m_blockNum;
    std::unordered_map<std::string, TableBase::Ptr> m_name2Table;
    std::vector<Change> m_changeLog;
    h256 m_hash;
    std::vector<std::string> m_sysTables;
    int createTableCode;
};

}  // namespace storage

}  // namespace dev
