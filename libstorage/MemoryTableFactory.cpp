/*
    This file is part of FISCO-BCOS.

    FISCO-BCOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FISCO-BCOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file MemoryTableFactory.h
 *  @author ancelmo
 *  @date 20180921
 */
#include "MemoryTableFactory.h"
#include "MemoryTable.h"
#include "TablePrecompiled.h"
#include <libblockverifier/ExecutiveContext.h>
#include <libdevcore/easylog.h>
#include <libdevcrypto/Hash.h>
#include <boost/algorithm/string.hpp>

using namespace dev;
using namespace dev::storage;
using namespace std;

Table::Ptr MemoryTableFactory::openTable(h256 blockHash, int64_t num, const string& table)
{
    LOG(DEBUG) << "Open table:" << blockHash << " num:" << num << " table:" << table;

    MemoryTable::Ptr memoryTable = make_shared<MemoryTable>();
    memoryTable->setStateStorage(m_stateStorage);
    memoryTable->setBlockHash(m_blockHash);
    memoryTable->setBlockNum(m_blockNum);
    memoryTable->setRecorder([&](Table::Ptr _table, Change::Kind _kind, string const& _key,
                                 vector<Change::Record>& _records) {
        m_changeLog.emplace_back(_table, _kind, _key, _records);
    });

    memoryTable->init(table);

    return memoryTable;
}

Table::Ptr MemoryTableFactory::createTable(h256 blockHash, int64_t num, const string& tableName,
    const string& keyField, const vector<string>& valueField)
{
    LOG(DEBUG) << "Create Table:" << blockHash << " num:" << num << " table:" << tableName;

    auto sysTable = openTable(blockHash, num, "_sys_tables_");

    // To make sure the table exists
    auto tableEntries = sysTable->select(tableName, sysTable->newCondition());
    if (tableEntries->size() == 0)
    {
        // Write table entry
        auto tableEntry = sysTable->newEntry();
        // tableEntry->setField("name", tableName);
        tableEntry->setField("key_field", keyField);
        tableEntry->setField("value_field", boost::join(valueField, ","));
        sysTable->insert(tableName, tableEntry);
    }

    return openTable(blockHash, num, tableName);
}

void MemoryTableFactory::setBlockHash(h256 blockHash)
{
    m_blockHash = blockHash;
}

void MemoryTableFactory::setBlockNum(int blockNum)
{
    m_blockNum = blockNum;
}

Address MemoryTableFactory::getTable(const string& tableName)
{
    auto it = m_name2Table.find(tableName);
    if (it == m_name2Table.end())
    {
        return Address();
    }
    return it->second;
}

void MemoryTableFactory::insertTable(const string& _tableName, const Address& _address)
{
    m_name2Table.insert(make_pair(_tableName, _address));
}

h256 MemoryTableFactory::hash(shared_ptr<blockverifier::ExecutiveContext> context)
{
    bytes data;
    LOG(DEBUG) << "this: " << this << " total table number:" << m_name2Table.size();
    for (auto tableAddress : m_name2Table)
    {
        blockverifier::TablePrecompiled::Ptr table =
            dynamic_pointer_cast<blockverifier::TablePrecompiled>(
                context->getPrecompiled(tableAddress.second));
        h256 hash = table->hash();
        LOG(DEBUG) << "table:" << tableAddress.first << " hash:" << hash;
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
    LOG(DEBUG) << "MemoryTableFactory data:" << data << " hash:" << dev::sha256(&data);
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

void MemoryTableFactory::commitDB(shared_ptr<blockverifier::ExecutiveContext> context, bool commit)
{
    if (!commit)
    {
        LOG(DEBUG) << "Clear m_name2Table: " << m_name2Table.size();
        m_name2Table.clear();
        return;
    }

    LOG(DEBUG) << "Submiting TablePrecompiled";


    vector<dev::storage::TableData::Ptr> datas;

    for (auto dbIt : m_name2Table)
    {
        blockverifier::TablePrecompiled::Ptr tablePrecompiled =
            dynamic_pointer_cast<blockverifier::TablePrecompiled>(
                context->getPrecompiled(dbIt.second));

        dev::storage::TableData::Ptr tableData = make_shared<dev::storage::TableData>();
        tableData->tableName = dbIt.first;

        bool dirtyTable = false;
        for (auto it : *(tablePrecompiled->getTable()->data()))
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

    LOG(DEBUG) << "Total: " << datas.size() << " key";
    if (!datas.empty())
    {
        if (m_hash == h256())
        {
            hash(context);
        }
        LOG(DEBUG) << "Submit data:" << datas.size() << " hash:" << m_hash;
        stateStorage()->commit(context->blockInfo().hash,
            context->blockInfo().number.convert_to<int64_t>(), datas, context->blockInfo().hash);
    }

    m_name2Table.clear();
    m_changeLog.clear();
}
