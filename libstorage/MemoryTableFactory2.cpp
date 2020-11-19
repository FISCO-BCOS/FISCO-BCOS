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
#include <libblockverifier/ExecutiveContext.h>
#include <libdevcrypto/CryptoInterface.h>
#include <libutilities/Common.h>
#include <libutilities/FixedHash.h>
#include <tbb/concurrent_vector.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_sort.h>
#include <boost/algorithm/string.hpp>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

using namespace bcos;
using namespace bcos::storage;
using namespace std;

MemoryTableFactory2::MemoryTableFactory2()
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
    m_sysTables.push_back(SYS_HASH_2_BLOCKHEADER);
}


void MemoryTableFactory2::init()
{
    auto table = openTable(SYS_CURRENT_STATE, false);
    auto condition = table->newCondition();
    condition->EQ(SYS_KEY, SYS_KEY_CURRENT_ID);
    // get id from backend
    auto out = table->select(SYS_KEY_CURRENT_ID, condition);
    if (out->size() > 0)
    {
        auto entry = out->get(0);
        auto numStr = entry->getField(SYS_VALUE);
        m_ID = boost::lexical_cast<size_t>(numStr);
    }
    if (g_BCOSConfig.version() >= V2_2_0)
    {
        m_ID = m_ID > ENTRY_ID_START ? m_ID : m_ID + ENTRY_ID_START;
    }
}

Table::Ptr MemoryTableFactory2::openTable(const std::string& _tableName, bool _authorityFlag, bool)
{
    tbb::spin_mutex::scoped_lock l(x_name2Table);
    return openTableWithoutLock(_tableName, _authorityFlag);
}

Table::Ptr MemoryTableFactory2::openTableWithoutLock(
    const std::string& tableName, bool authorityFlag, bool)
{
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
        auto tempSysTable = openTableWithoutLock(SYS_TABLES);
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
    tableInfo->fields.emplace_back(NUM_FIELD);
    tableInfo->fields.emplace_back(ID_FIELD);

    auto memoryTable2 = std::make_shared<MemoryTable2>();
    Table::Ptr memoryTable = memoryTable2;
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
    {
        tbb::spin_mutex::scoped_lock l(x_name2Table);
        auto tableEntries = sysTable->select(tableName, sysTable->newCondition());
        if (tableEntries->size() != 0)
        {
            STORAGE_LOG(WARNING) << LOG_BADGE("MemoryTableFactory2")
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
                                 << LOG_KV("origin", _origin.hex())
                                 << LOG_KV("table name", tableName);
            BOOST_THROW_EXCEPTION(StorageException(result, "create table permission denied"));
        }
        STORAGE_LOG(INFO) << LOG_BADGE("MemoryTableFactory2") << LOG_DESC("createTable")
                          << LOG_KV("table name", tableName) << LOG_KV("keyField", keyField)
                          << LOG_KV("valueField", valueField);
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

h256 MemoryTableFactory2::hash()
{
    std::vector<std::pair<std::string, Table::Ptr> > tables;
    for (auto& it : m_name2Table)
    {
        if (it.second->tableInfo()->enableConsensus)
        {
            if (g_BCOSConfig.version() >= V2_5_0 && !it.second->dirty())
            {  // clean table dont calculate hash
                continue;
            }
            tables.push_back(std::make_pair(it.first, it.second));
        }
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
#if FISCO_DEBUG
                    STORAGE_LOG(DEBUG) << LOG_BADGE("FISCO_DEBUG")
                                       << LOG_BADGE("MemoryTableFactory2 hash continued ") << it + 1
                                       << "/" << tables.size() << LOG_KV("tableName", table.first)
                                       << LOG_KV("blockNum", m_blockNum);
#endif
                    continue;
                }

                bytes tableHash = hash.asBytes();
                memcpy(&data[it * 32], &tableHash[0], tableHash.size());
#if FISCO_DEBUG
                STORAGE_LOG(DEBUG)
                    << LOG_BADGE("FISCO_DEBUG") << LOG_BADGE("MemoryTableFactory2 hash ") << it + 1
                    << "/" << tables.size() << LOG_KV("tableName", table.first)
                    << LOG_KV("hash", hash) << LOG_KV("blockNum", m_blockNum);
#endif
            }
        });

    if (data.empty())
    {
        return h256();
    }
    if (g_BCOSConfig.version() <= V2_4_0)
    {
        if (g_BCOSConfig.SMCrypto())
        {
            m_hash = bcos::sm3(&data);
        }
        else
        {
            // in previous version(<= 2.4.0), we use sha256(...) to calculate hash of the data,
            // for now, to keep consistent with transction's implementation, we decide to use
            // keccak256(...) to calculate hash of the data. This `else` branch is just for
            // compatibility.
            m_hash = bcos::sha256(&data);
        }
    }
    else
    {
        m_hash = crypto::Hash(&data);
    }
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

void MemoryTableFactory2::commitDB(bcos::h256 const&, int64_t _blockNumber)
{
    auto start_time = utcTime();
    auto record_time = utcTime();
    vector<bcos::storage::TableData::Ptr> datas;

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
    tbb::parallel_sort(datas.begin(), datas.end(),
        [](const bcos::storage::TableData::Ptr& lhs, const bcos::storage::TableData::Ptr& rhs) {
            return lhs->info->name < rhs->info->name;
        });
    ssize_t currentStateIdx = -1;
    for (size_t i = 0; i < datas.size(); ++i)
    {
        auto tableData = datas[i];
        if (currentStateIdx < 0 && tableData->info->name == SYS_CURRENT_STATE)
        {
            currentStateIdx = i;
        }
        for (auto entry = tableData->newEntries->begin(); entry != tableData->newEntries->end();
             ++entry)
        {
            (*entry)->setID(++m_ID);
        }
    }

    // Write m_ID to SYS_CURRENT_STATE
    Entry::Ptr idEntry = std::make_shared<Entry>();
    idEntry->setID(1);
    idEntry->setNum(_blockNumber);
    idEntry->setStatus(0);
    idEntry->setField(SYS_KEY, SYS_KEY_CURRENT_ID);
    idEntry->setField("value", boost::lexical_cast<std::string>(m_ID));
    TableData::Ptr currentState;
    if (currentStateIdx == -1)
    {
        STORAGE_LOG(FATAL) << "Can't find current state table";
    }
    currentState = datas[currentStateIdx];
    if (_blockNumber == 0)
    {
        idEntry->setForce(true);
        currentState->newEntries->addEntry(idEntry);
    }
    else
    {
        currentState->dirtyEntries->addEntry(idEntry);
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

void MemoryTableFactory2::setAuthorizedAddress(storage::TableInfo::Ptr _tableInfo)
{
    typename Table::Ptr accessTable = openTableWithoutLock(SYS_ACCESS_TABLE);
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
