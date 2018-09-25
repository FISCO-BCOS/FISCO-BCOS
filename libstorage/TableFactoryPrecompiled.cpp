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
/** @file TableFactoryPrecompiled.h
 *  @author ancelmo
 *  @date 20180921
 */
#include "TableFactoryPrecompiled.h"
#include "MemoryTable.h"
#include "TablePrecompiled.h"
#include <libdevcore/easylog.h>
#include <libdevcrypto/Common.h>
#include <libdevcrypto/Hash.h>
#include <libethcore/ABI.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

using namespace dev;
using namespace dev::blockverifier;
using namespace std;


std::string TableFactoryPrecompiled::toString(std::shared_ptr<ExecutiveContext>)
{
    return "Tableactory";
}

bytes TableFactoryPrecompiled::call(std::shared_ptr<ExecutiveContext> context, bytesConstRef param)
{
    LOG(TRACE) << "this: " << this << " call TableFactory:" << toHex(param);


    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    LOG(DEBUG) << "func:" << hex << func;

    dev::eth::ContractABI abi;
    bytes out;

    switch (func)
    {
    case 0xc184e0ff:  // openDB(string)
    case 0xf23f63c9:
    {  // openTable(string)
        string tableName;
        abi.abiOut(data, tableName);

        LOG(DEBUG) << "DBFactory open table:" << tableName;

        Address address = openTable(context, tableName);
        out = abi.abiIn("", address);
        break;
    }
    case 0x56004b6a:
    {  // createTable(string,string,string)
        string tableName;
        string keyName;
        string fieldNames;

        abi.abiOut(data, tableName, keyName, fieldNames);
        vector<string> fieldNameList;
        boost::split(fieldNameList, fieldNames, boost::is_any_of(","));
        for (auto& str : fieldNameList)
            boost::trim(str);
        string valueFiled = boost::join(fieldNameList, ",");
        auto errorCode = isTableCreated(context, tableName, keyName, valueFiled);

        // not exist
        if (errorCode == 0u)
        {
            auto sysTable = getSysTable(context);
            auto tableEntry = sysTable->getTable()->newEntry();
            tableEntry->setField("table_name", tableName);
            tableEntry->setField("key_field", keyName);
            tableEntry->setField("value_field", valueFiled);
            sysTable->getTable()->insert(tableName, tableEntry);
        }
        out = abi.abiIn("", errorCode);
        break;
    }
    default:
    {
        break;
    }
    }

    return out;
}

TablePrecompiled::Ptr TableFactoryPrecompiled::getSysTable(ExecutiveContext::Ptr context)
{
    std::string tableName = "_sys_tables_";
    auto it = m_name2Table.find(tableName);
    if (it == m_name2Table.end())
    {
        dev::storage::Table::Ptr table = m_MemoryTableFactory->openTable(
            context->blockInfo().hash, context->blockInfo().number.convert_to<int>(), tableName);

        TablePrecompiled::Ptr tablePrecompiled = std::make_shared<TablePrecompiled>();
        tablePrecompiled->setTable(table);

        Address address = context->registerPrecompiled(tablePrecompiled);
        m_name2Table.insert(std::make_pair(tableName, address));

        return tablePrecompiled;
    }
    LOG(DEBUG) << "Table _sys_tables_:" << context->blockInfo().hash
               << " already opened:" << it->second;
    return std::dynamic_pointer_cast<TablePrecompiled>(context->getPrecompiled(it->second));
}

unsigned TableFactoryPrecompiled::isTableCreated(ExecutiveContext::Ptr context,
    const string& tableName, const string& keyField, const string& valueFiled)
{
    auto it = m_name2Table.find(tableName);
    if (it != m_name2Table.end())
        return TABLE_ALREADY_OPEN;
    auto sysTable = getSysTable(context);
    auto tableEntries =
        sysTable->getTable()->select(tableName, sysTable->getTable()->newCondition());
    if (tableEntries->size() == 0u)
        return TABLE_NOT_EXISTS;
    for (size_t i = 0; i < tableEntries->size(); ++i)
    {
        auto entry = tableEntries->get(i);
        if (keyField == entry->getField("key_field") &&
            valueFiled == entry->getField("value_field"))
            return TABLENAME_ALREADY_EXISTS;
    }
    return TABLENAME_CONFLICT;
}

Address TableFactoryPrecompiled::openTable(ExecutiveContext::Ptr context, const string& tableName)
{
    auto it = m_name2Table.find(tableName);
    if (it != m_name2Table.end())
    {
        LOG(DEBUG) << "Table:" << context->blockInfo().hash << " already opened:" << it->second;
        return it->second;
    }
    auto sysTable = getSysTable(context);
    auto tableEntries =
        sysTable->getTable()->select(tableName, sysTable->getTable()->newCondition());
    if (tableEntries->size() == 0u)
        return Address();

    LOG(DEBUG) << "Open new table:" << tableName;
    dev::storage::Table::Ptr table = m_MemoryTableFactory->openTable(
        context->blockInfo().hash, context->blockInfo().number.convert_to<int>(), tableName);
    TablePrecompiled::Ptr tablePrecompiled = std::make_shared<TablePrecompiled>();
    tablePrecompiled->setTable(table);
    Address address = context->registerPrecompiled(tablePrecompiled);
    m_name2Table.insert(std::make_pair(tableName, address));
    return address;
}

h256 TableFactoryPrecompiled::hash(std::shared_ptr<ExecutiveContext> context)
{
    bytes data;

    LOG(DEBUG) << "this: " << this << " total table number:" << m_name2Table.size();
    for (auto tableAddress : m_name2Table)
    {
        TablePrecompiled::Ptr table = std::dynamic_pointer_cast<TablePrecompiled>(
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

    LOG(DEBUG) << "TableFactoryPrecompiled data:" << data << " hash:" << dev::sha256(&data);

    m_hash = dev::sha256(&data);
    return m_hash;
}
