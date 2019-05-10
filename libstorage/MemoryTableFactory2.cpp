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
/** @file MemoryTableFactory2.h
 *  @author ancelmo
 *  @date 20180921
 */
#include "MemoryTableFactory2.h"
#include "Common.h"
#include "MemoryTable2.h"
#include "StorageException.h"
#include "TablePrecompiled.h"
#include <libblockverifier/ExecutiveContext.h>
#include <libdevcore/Common.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/easylog.h>
#include <libdevcrypto/Hash.h>
#include <tbb/concurrent_vector.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_sort.h>
#include <boost/algorithm/string.hpp>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

using namespace dev;
using namespace dev::storage;
using namespace std;

MemoryTableFactory2::MemoryTableFactory2() : m_blockHash(h256(0)), m_blockNum(0)
{
    m_sysTables.push_back(SYS_CONSENSUS);
    m_sysTables.push_back(SYS_TABLES);
    m_sysTables.push_back(SYS_ACCESS_TABLE);
    m_sysTables.push_back(SYS_CURRENT_STATE);
    m_sysTables.push_back(SYS_NUMBER_2_HASH);
    m_sysTables.push_back(SYS_TX_HASH_2_BLOCK);
    m_sysTables.push_back(SYS_HASH_2_BLOCK);
    m_sysTables.push_back(SYS_CNS);
    m_sysTables.push_back(SYS_CONFIG);
    m_sysTables.push_back(SYS_BLOCK_2_NONCES);
}


Table::Ptr MemoryTableFactory2::openTable(
    const std::string& tableName, bool authorityFlag, bool isPara)
{
    RecursiveGuard l(x_name2Table);
    auto it = m_name2Table.find(tableName);
    if (it != m_name2Table.end())
    {
        return it->second;
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
    tableInfo->fields.emplace_back("_id_");

    Table::Ptr memoryTable = nullptr;
    if (isPara)
    {
        memoryTable = std::make_shared<MemoryTable2>();
    }
    else
    {
        memoryTable = std::make_shared<MemoryTable2>();
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
    memoryTable->setRecorder([&](Table::Ptr _table, Change::Kind _kind, std::string const& _key,
                                 std::vector<Change::Record>& _records) {
        auto& changeLog = getChangeLog();
        changeLog.emplace_back(_table, _kind, _key, _records);
    });

    m_name2Table.insert({tableName, memoryTable});
    return memoryTable;
}

Table::Ptr MemoryTableFactory2::createTable(const std::string& tableName,
    const std::string& keyField, const std::string& valueField, bool authorityFlag,
    Address const& _origin, bool isPara)
{
    auto sysTable = openTable(SYS_TABLES, authorityFlag);
    // To make sure the table exists
    auto tableEntries = sysTable->select(tableName, sysTable->newCondition());
    if (tableEntries->size() != 0)
    {
        STORAGE_LOG(ERROR) << LOG_BADGE("MemoryTableFactory2")
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
        STORAGE_LOG(WARNING) << LOG_BADGE("MemoryTableFactory2")
                             << LOG_DESC("create table permission denied")
                             << LOG_KV("origin", _origin.hex()) << LOG_KV("table name", tableName);
        BOOST_THROW_EXCEPTION(StorageException(result, "create table permission denied"));
    }
    return openTable(tableName, authorityFlag, isPara);
}

size_t MemoryTableFactory2::savepoint()
{
    auto& changeLog = getChangeLog();
    return changeLog.size();
}

void MemoryTableFactory2::commit()
{
    getChangeLog().clear();
}

void MemoryTableFactory2::setBlockHash(h256 blockHash)
{
    m_blockHash = blockHash;
}

void MemoryTableFactory2::setBlockNum(int64_t blockNum)
{
    m_blockNum = blockNum;
}

h256 MemoryTableFactory2::hash()
{
    std::vector<std::pair<std::string, Table::Ptr> > tables;
    for (auto it : m_name2Table)
    {
        tables.push_back(std::make_pair(it.first, it.second));
    }

    tbb::parallel_sort(tables.begin(), tables.end(),
        [](const std::pair<std::string, Table::Ptr>& lhs,
            const std::pair<std::string, Table::Ptr>& rhs) { return lhs.first < rhs.first; });

    bytes data;
    data.resize(tables.size() * 32);
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, tables.size()), [&](const tbb::blocked_range<size_t>& range) {
            for (auto it = range.begin(); it != range.end(); ++it)
            {
                auto table = tables[it];
                h256 hash = table.second->hash();
                if (hash == h256())
                {
                    continue;
                }

                bytes tableHash = hash.asBytes();

                memcpy(&data[it * 32], &tableHash[0], tableHash.size());
            }
        });

    if (data.empty())
    {
        return h256();
    }
    m_hash = dev::sha256(&data);
    return m_hash;
}

std::vector<Change>& MemoryTableFactory2::getChangeLog()
{
    return s_changeLog.local();
}

void MemoryTableFactory2::rollback(size_t _savepoint)
{
    auto& changeLog = getChangeLog();
    while (_savepoint < changeLog.size())
    {
        auto change = changeLog.back();

        // Public MemoryTable API cannot be used here because it will add another
        // change log entry.
        change.table->rollback(change);

        changeLog.pop_back();
    }
}

void MemoryTableFactory2::commitDB(dev::h256 const& _blockHash, int64_t _blockNumber)
{
    auto start_time = utcTime();
    auto record_time = utcTime();
    vector<dev::storage::TableData::Ptr> datas;

    for (auto& dbIt : m_name2Table)
    {
        auto table = std::dynamic_pointer_cast<Table>(dbIt.second);

        STORAGE_LOG(TRACE) << "Dumping table: " << dbIt.first;
        auto tableData = table->dump();

        if (tableData && (tableData->dirtyEntries->size() > 0 || tableData->newEntries->size() > 0))
        {
            datas.push_back(tableData);
        }
    }
    auto getData_time_cost = utcTime() - record_time;
    record_time = utcTime();

    if (!datas.empty())
    {
        stateStorage()->commit(_blockHash, _blockNumber, datas);
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

storage::TableInfo::Ptr MemoryTableFactory2::getSysTableInfo(const std::string& tableName)
{
    auto tableInfo = make_shared<storage::TableInfo>();
    tableInfo->name = tableName;
    if (tableName == SYS_CONSENSUS)
    {
        tableInfo->key = "name";
        tableInfo->fields = vector<string>{"type", "node_id", "enable_num"};
    }
    else if (tableName == SYS_TABLES)
    {
        tableInfo->key = "table_name";
        tableInfo->fields = vector<string>{"key_field", "value_field"};
    }
    else if (tableName == SYS_ACCESS_TABLE)
    {
        tableInfo->key = "table_name";
        tableInfo->fields = vector<string>{"address", "enable_num"};
    }
    else if (tableName == SYS_CURRENT_STATE)
    {
        tableInfo->key = SYS_KEY;
        tableInfo->fields = std::vector<std::string>{"value"};
    }
    else if (tableName == SYS_NUMBER_2_HASH)
    {
        tableInfo->key = "number";
        tableInfo->fields = std::vector<std::string>{"value"};
    }
    else if (tableName == SYS_TX_HASH_2_BLOCK)
    {
        tableInfo->key = "hash";
        tableInfo->fields = std::vector<std::string>{"value", "index"};
    }
    else if (tableName == SYS_HASH_2_BLOCK)
    {
        tableInfo->key = "hash";
        tableInfo->fields = std::vector<std::string>{"value"};
    }
    else if (tableName == SYS_CNS)
    {
        tableInfo->key = "name";
        tableInfo->fields = std::vector<std::string>{"version", "address", "abi"};
    }
    else if (tableName == SYS_CONFIG)
    {
        tableInfo->key = "key";
        tableInfo->fields = std::vector<std::string>{"value", "enable_num"};
    }
    else if (tableName == SYS_BLOCK_2_NONCES)
    {
        tableInfo->key = "number";
        tableInfo->fields = std::vector<std::string>{SYS_VALUE};
    }
    return tableInfo;
}


void MemoryTableFactory2::setAuthorizedAddress(storage::TableInfo::Ptr _tableInfo)
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
