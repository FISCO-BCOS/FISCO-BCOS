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
#include "MemoryTableFactory.h"
#include "Common.h"
#include "MemoryTable.h"
#include "StorageException.h"
#include <libblockverifier/ExecutiveContext.h>
#include <libdevcore/FixedHash.h>
#include <libdevcrypto/Hash.h>
#include <boost/algorithm/string.hpp>
#include <memory>
#include <utility>
#include <vector>

using namespace dev;
using namespace dev::storage;
using namespace std;

const std::vector<string> MemoryTableFactory::c_sysTables = std::vector<string>{SYS_CONSENSUS,
    SYS_TABLES, SYS_ACCESS_TABLE, SYS_CURRENT_STATE, SYS_NUMBER_2_HASH, SYS_TX_HASH_2_BLOCK,
    SYS_HASH_2_BLOCK, SYS_CNS, SYS_CONFIG, SYS_BLOCK_2_NONCES};

// according to
// https://fisco-bcos-documentation.readthedocs.io/zh_CN/latest/docs/design/security_control/permission_control.html
const std::vector<string> MemoryTableFactory::c_sysNonChangeLogTables =
    std::vector<string>{SYS_CURRENT_STATE, SYS_TX_HASH_2_BLOCK, SYS_NUMBER_2_HASH, SYS_HASH_2_BLOCK,
        SYS_BLOCK_2_NONCES};

MemoryTableFactory::MemoryTableFactory() {}

Table::Ptr MemoryTableFactory::openTable(
    const std::string& tableName, bool authorityFlag, bool isPara)
{
    RecursiveGuard l(x_name2Table);
    auto it = m_name2Table.find(tableName);
    if (it != m_name2Table.end())
    {
        return it->second;
    }
    auto tableInfo = std::make_shared<storage::TableInfo>();

    if (c_sysTables.end() != find(c_sysTables.begin(), c_sysTables.end(), tableName))
    {
        tableInfo = getSysTableInfo(tableName);
    }
    else
    {
        auto tempSysTable = openTable(SYS_TABLES);
        auto tableEntries = tempSysTable->select(tableName, tempSysTable->newCondition());
        if (tableEntries->size() == 0u)
        {
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
    tableInfo->fields.emplace_back(NUM_FIELD);

    Table::Ptr memoryTable = nullptr;
    if (isPara)
    {
        memoryTable = std::make_shared<MemoryTable<Parallel>>();
    }
    else
    {
        memoryTable = std::make_shared<MemoryTable<Serial>>();
    }

    memoryTable->setStateStorage(m_stateStorage);
    memoryTable->setBlockHash(m_blockHash);
    memoryTable->setBlockNum(m_blockNum);
    memoryTable->setTableInfo(tableInfo);

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
            auto tableEntries = memoryTable->select(SYS_ACCESS_TABLE, memoryTable->newCondition());
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

    if (std::find(c_sysNonChangeLogTables.begin(), c_sysNonChangeLogTables.end(), tableName) ==
        c_sysNonChangeLogTables.end())
    {
        memoryTable->setRecorder(
            [&, this](Table::Ptr _table, Change::Kind _kind, std::string const& _key,
                std::vector<Change::Record>& _records) {
                auto& changeLog = getChangeLog();
                changeLog.emplace_back(_table, _kind, _key, _records);
            });
    }

    m_name2Table.insert({tableName, memoryTable});
    return memoryTable;
}

Table::Ptr MemoryTableFactory::createTable(const std::string& tableName,
    const std::string& keyField, const std::string& valueField, bool authorityFlag,
    Address const& _origin, bool isPara)
{
    RecursiveGuard l(x_name2Table);

    auto sysTable = openTable(SYS_TABLES, authorityFlag);
    // To make sure the table exists
    auto tableEntries = sysTable->select(tableName, sysTable->newCondition());
    if (tableEntries->size() != 0)
    {
        STORAGE_LOG(ERROR) << LOG_BADGE("MemoryTableFactory")
                           << LOG_DESC("table already exist in _sys_tables_")
                           << LOG_KV("table name", tableName);
        return nullptr;
    }
    // Write table entry
    auto tableEntry = sysTable->newEntry();
    tableEntry->setField("table_name", tableName);
    tableEntry->setField("key_field", keyField);
    tableEntry->setField("value_field", valueField);
    auto result = sysTable->insert(
        tableName, tableEntry, std::make_shared<AccessOptions>(_origin, authorityFlag));
    if (result == storage::CODE_NO_AUTHORIZED)
    {
        STORAGE_LOG(WARNING) << LOG_BADGE("MemoryTableFactory")
                             << LOG_DESC("create table permission denied")
                             << LOG_KV("origin", _origin.hex()) << LOG_KV("table name", tableName);

        BOOST_THROW_EXCEPTION(StorageException(result, "create table permission denied"));
    }
    return openTable(tableName, authorityFlag, isPara);
}

size_t MemoryTableFactory::savepoint()
{
    auto& changeLog = getChangeLog();
    return changeLog.size();
}

h256 MemoryTableFactory::hash()
{
    bytes data;
    for (auto& it : m_name2Table)
    {
        auto table = it.second;
        h256 hash = table->hash();
        if (hash == h256())
        {
            continue;
        }

        bytes tableHash = hash.asBytes();
        // LOG(DEBUG) << LOG_BADGE("Report") << LOG_DESC("tableHash")
        //<< LOG_KV(it.first, dev::sha256(ref(tableHash)));

        data.insert(data.end(), tableHash.begin(), tableHash.end());
    }
    if (data.empty())
    {
        return h256();
    }
    m_hash = dev::sha256(&data);
    return m_hash;
}

std::vector<Change>& MemoryTableFactory::getChangeLog()
{
    return s_changeLog.local();
}

void MemoryTableFactory::rollback(size_t _savepoint)
{
    auto& changeLog = getChangeLog();
    while (_savepoint < changeLog.size())
    {
        auto& change = changeLog.back();

        // Public MemoryTable API cannot be used here because it will add another
        // change log entry.
        change.table->rollback(change);

        changeLog.pop_back();
    }
}

void MemoryTableFactory::commit()
{
    getChangeLog().clear();
}

void MemoryTableFactory::commitDB(h256 const&, int64_t _blockNumber)
{
    auto start_time = utcTime();
    auto record_time = utcTime();
    vector<dev::storage::TableData::Ptr> datas;

    for (auto& dbIt : m_name2Table)
    {
        auto table = std::dynamic_pointer_cast<Table>(dbIt.second);

        auto tableData = table->dump();
        tableData->tableName = dbIt.first;

        if (!tableData->data.empty())
        {
            datas.push_back(tableData);
        }
    }
    auto getData_time_cost = utcTime() - record_time;
    record_time = utcTime();

    if (!datas.empty())
    {
        stateStorage()->commit(_blockNumber, datas);
    }
    auto commit_time_cost = utcTime() - record_time;
    record_time = utcTime();

    m_name2Table.clear();
    auto clear_time_cost = utcTime() - record_time;
    STORAGE_LOG(DEBUG) << LOG_BADGE("Commit") << LOG_DESC("Commit db time record")
                       << LOG_KV("getDataTimeCost", getData_time_cost)
                       << LOG_KV("commitTimeCost", commit_time_cost)
                       << LOG_KV("clearTimeCost", clear_time_cost)
                       << LOG_KV("totalTimeCost", utcTime() - start_time);
}

void MemoryTableFactory::setAuthorizedAddress(storage::TableInfo::Ptr _tableInfo)
{
    typename Table::Ptr accessTable = openTable(SYS_ACCESS_TABLE);
    if (accessTable)
    {
        auto tableEntries = accessTable->select(_tableInfo->name, accessTable->newCondition());
        for (size_t i = 0; i < tableEntries->size(); ++i)
        {
            auto entry = tableEntries->get(i);
            if (std::stoi(entry->getField("enable_num")) <= m_blockNum)
            {
                _tableInfo->authorizedAddress.emplace_back(entry->getField("address"));
            }
        }
    }
}
