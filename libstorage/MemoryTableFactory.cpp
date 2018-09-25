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
#include <libdevcore/easylog.h>
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
