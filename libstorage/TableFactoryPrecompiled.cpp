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
#include <libethcore/ABI.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

using namespace dev;
using namespace dev::blockverifier;
using namespace std;

TableFactoryPrecompiled::TableFactoryPrecompiled()
{
    m_sysTables.push_back("_sys_tables_");
    m_sysTables.push_back("_sys_miners_");
}

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
            auto sysTable = std::dynamic_pointer_cast<TablePrecompiled>(
                context->getPrecompiled(getSysTable(context, "_sys_tables_")));
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

Address TableFactoryPrecompiled::getSysTable(
    ExecutiveContext::Ptr context, const std::string& _tableName)
{
    if (m_sysTables.end() == find(m_sysTables.begin(), m_sysTables.end(), _tableName))
        return Address();

    auto address = m_MemoryTableFactory->getTable(_tableName);
    if (address == Address())
    {
        dev::storage::Table::Ptr table = m_MemoryTableFactory->openTable(context->blockInfo().hash,
            context->blockInfo().number.convert_to<int64_t>(), _tableName);

        TablePrecompiled::Ptr tablePrecompiled = std::make_shared<TablePrecompiled>();
        tablePrecompiled->setTable(table);

        address = context->registerPrecompiled(tablePrecompiled);
        m_MemoryTableFactory->insertTable(_tableName, address);
    }

    return address;
}

unsigned TableFactoryPrecompiled::isTableCreated(ExecutiveContext::Ptr context,
    const string& tableName, const string& keyField, const string& valueFiled)
{
    auto address = m_MemoryTableFactory->getTable(tableName);
    if (address != Address())
        return TABLE_ALREADY_OPEN;
    auto sysTable = std::dynamic_pointer_cast<TablePrecompiled>(
        context->getPrecompiled(getSysTable(context, "_sys_tables_")));
    auto tableEntries =
        sysTable->getTable()->select(tableName, sysTable->getTable()->newCondition());
    if (tableEntries->size() == 0u)
        return TABLE_NOT_EXISTS;
    for (size_t i = 0; i < tableEntries->size(); ++i)
    {
        auto entry = tableEntries->get(i);
        if (keyField == entry->getField("key_field") &&
            valueFiled == entry->getField("value_field"))
        {
            LOG(DEBUG) << "TableFactory table already exists:" << tableName;
            return TABLENAME_ALREADY_EXISTS;
        }
    }
    LOG(DEBUG) << "TableFactory table conflict:" << tableName;
    return TABLENAME_CONFLICT;
}

Address TableFactoryPrecompiled::openTable(ExecutiveContext::Ptr context, const string& tableName)
{
    auto address = m_MemoryTableFactory->getTable(tableName);
    if (address != Address())
    {
        LOG(DEBUG) << "Table:" << context->blockInfo().hash << " already opened:" << address;
        return address;
    }

    if (m_sysTables.end() == find(m_sysTables.begin(), m_sysTables.end(), tableName))
    {
        auto sysTable = std::dynamic_pointer_cast<TablePrecompiled>(
            context->getPrecompiled(getSysTable(context, "_sys_tables_")));
        auto tableEntries =
            sysTable->getTable()->select(tableName, sysTable->getTable()->newCondition());
        if (tableEntries->size() == 0u)
        {
            LOG(DEBUG) << tableName << "not exist.";
            return Address();
        }
    }

    LOG(DEBUG) << "Open new sys table:" << tableName;
    return getSysTable(context, tableName);
}

h256 TableFactoryPrecompiled::hash(std::shared_ptr<ExecutiveContext> context)
{
    auto hash = m_MemoryTableFactory->hash(context);
    if (hash == h256())
        return hash;
    m_hash = hash;
    return m_hash;
}
