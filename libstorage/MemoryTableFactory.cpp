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
#include "TablePrecompiled.h"
#include <libblockverifier/ExecutiveContext.h>
#include <libdevcore/easylog.h>
#include <libdevcrypto/Hash.h>
#include <boost/algorithm/string.hpp>

using namespace dev;
using namespace dev::storage;
using namespace std;

MemoryTableFactory::MemoryTableFactory() : m_blockHash(h256(0)), m_blockNum(0)
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

Table::Ptr MemoryTableFactory::openTable(const string& tableName, bool authorityFlag)
{
    auto it = m_name2Table.find(tableName);
    if (it != m_name2Table.end())
    {
        return it->second;
    }
    auto tableInfo = make_shared<storage::TableInfo>();

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
            STORAGE_LOG(DEBUG) << LOG_BADGE("MemoryTableFactory")
                               << LOG_DESC("table doesn't exist in _sys_tables_")
                               << LOG_KV("table name", tableName);
            return nullptr;
        }
        auto entry = tableEntries->get(0);
        tableInfo->name = tableName;
        tableInfo->key = entry->getField("key_field");
        string valueFields = entry->getField("value_field");
        boost::split(tableInfo->fields, valueFields, boost::is_any_of(","));
    }
    tableInfo->fields.emplace_back(STATUS);
    tableInfo->fields.emplace_back(tableInfo->key);
    tableInfo->fields.emplace_back("_hash_");
    tableInfo->fields.emplace_back("_num_");

    MemoryTable::Ptr memoryTable = std::make_shared<MemoryTable>();
    memoryTable->setStateStorage(m_stateStorage);
    memoryTable->setBlockHash(m_blockHash);
    memoryTable->setBlockNum(m_blockNum);
    memoryTable->setTableInfo(tableInfo);

    // authority flag
    if (authorityFlag)
    {
        // set authorized address to memoryTable
        if (tableName != string(SYS_ACCESS_TABLE))
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

    memoryTable->setRecorder([&](Table::Ptr _table, Change::Kind _kind, string const& _key,
                                 vector<Change::Record>& _records) {
        m_changeLog.emplace_back(_table, _kind, _key, _records);
    });

    memoryTable->init(tableName);
    m_name2Table.insert({tableName, memoryTable});
    return memoryTable;
}

Table::Ptr MemoryTableFactory::createTable(const string& tableName, const string& keyField,
    const std::string& valueField, bool authorityFlag, Address const& _origin)
{
    STORAGE_LOG(DEBUG) << LOG_BADGE("MemoryTableFactory") << LOG_DESC("create table")
                       << LOG_KV("table name", tableName) << LOG_KV("blockHash", m_blockHash)
                       << LOG_KV("blockNum", m_blockNum);

    auto sysTable = openTable(SYS_TABLES, authorityFlag);

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
    createTableCode = sysTable->insert(
        tableName, tableEntry, std::make_shared<AccessOptions>(_origin, authorityFlag));
    if (createTableCode == storage::CODE_NO_AUTHORIZED)
    {
        STORAGE_LOG(WARNING) << LOG_BADGE("MemoryTableFactory")
                             << LOG_DESC("create table non-authorized")
                             << LOG_KV("origin", _origin.hex()) << LOG_KV("table name", tableName);
        return nullptr;
    }
    return openTable(tableName);
}

void MemoryTableFactory::setBlockHash(h256 blockHash)
{
    m_blockHash = blockHash;
}

void MemoryTableFactory::setBlockNum(int64_t blockNum)
{
    m_blockNum = blockNum;
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

        bytes tableHash = table->hash().asBytes();
        data.insert(data.end(), tableHash.begin(), tableHash.end());
    }
    if (data.empty())
    {
        return h256();
    }
    m_hash = dev::sha256(&data);
    return m_hash;
}

void MemoryTableFactory::rollback(size_t _savepoint)
{
    while (_savepoint < m_changeLog.size())
    {
        auto& change = m_changeLog.back();

        // Public MemoryTable API cannot be used here because it will add another
        // change log entry.
        switch (change.kind)
        {
        case Change::Insert:
        {
            auto data = change.table->data();
            auto entries = (*data)[change.key];
            entries->removeEntry(change.value[0].index);
            if (entries->size() == 0u)
                data->erase(change.key);
            break;
        }
        case Change::Update:
        {
            auto data = change.table->data();
            auto entries = (*data)[change.key];
            for (auto& record : change.value)
            {
                auto entry = entries->get(record.index);
                entry->setField(record.key, record.oldValue);
            }
            break;
        }
        case Change::Remove:
        {
            auto data = change.table->data();
            auto entries = (*data)[change.key];
            for (auto& record : change.value)
            {
                auto entry = entries->get(record.index);
                entry->setStatus(0);
            }
            break;
        }
        case Change::Select:

        default:
            break;
        }
        m_changeLog.pop_back();
    }
}

void MemoryTableFactory::commit() {}

void MemoryTableFactory::commitDB(h256 const& _blockHash, int64_t _blockNumber)
{
    vector<dev::storage::TableData::Ptr> datas;

    for (auto& dbIt : m_name2Table)
    {
        auto table = dbIt.second;

        dev::storage::TableData::Ptr tableData = make_shared<dev::storage::TableData>();
        tableData->tableName = dbIt.first;

        bool dirtyTable = false;
        for (auto& it : *(table->data()))
        {
            tableData->data.insert(make_pair(it.first, it.second));

            if (it.second->dirty())
            {
                dirtyTable = true;
            }
        }

        if (!tableData->data.empty() && dirtyTable)
        {
            datas.push_back(tableData);
        }
    }

    if (!datas.empty())
    {
        /// STORAGE_LOG(DEBUG) << "Submit data:" << datas.size() << " hash:" << m_hash;
        stateStorage()->commit(_blockHash, _blockNumber, datas, _blockHash);
    }

    m_name2Table.clear();
    m_changeLog.clear();
}

storage::TableInfo::Ptr MemoryTableFactory::getSysTableInfo(const std::string& tableName)
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

void MemoryTableFactory::setAuthorizedAddress(storage::TableInfo::Ptr _tableInfo)
{
    Table::Ptr accessTable = openTable(SYS_ACCESS_TABLE);
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
