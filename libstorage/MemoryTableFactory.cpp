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

Table::Ptr MemoryTableFactory::openTable(h256 blockHash, int num, const std::string& table)
{
    LOG(DEBUG) << "Open table:" << blockHash << " num:" << num << " table:" << table;

    MemoryTable::Ptr memoryTable = std::make_shared<MemoryTable>();
    memoryTable->setStateStorage(m_stateStorage);
    memoryTable->setBlockHash(m_blockHash);
    memoryTable->setBlockNum(m_blockNum);

    memoryTable->init(table);

    return memoryTable;
}

Table::Ptr MemoryTableFactory::createTable(h256 blockHash, int num, const std::string& tableName,
    const std::string& keyField, const std::vector<std::string>& valueField)
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

Address MemoryTableFactory::getTable(const std::string& tableName)
{
    auto it = m_name2Table.find(tableName);
    if (it == m_name2Table.end())
    {
        return Address();
    }
    return it->second;
}

void MemoryTableFactory::insertTable(const std::string& _tableName, const Address& _address)
{
    m_name2Table.insert(std::make_pair(_tableName, _address));
}

h256 MemoryTableFactory::hash(std::shared_ptr<blockverifier::ExecutiveContext> context)
{
    bytes data;
    LOG(DEBUG) << "this: " << this << " total table number:" << m_name2Table.size();
    for (auto tableAddress : m_name2Table)
    {
        blockverifier::TablePrecompiled::Ptr table =
            std::dynamic_pointer_cast<blockverifier::TablePrecompiled>(
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

    return dev::sha256(&data);
}
