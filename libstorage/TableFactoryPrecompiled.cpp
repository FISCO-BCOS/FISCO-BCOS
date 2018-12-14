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
using namespace dev::storage;
std::string TableFactoryPrecompiled::toString(std::shared_ptr<ExecutiveContext>)
{
    return "TableFactory";
}

bytes TableFactoryPrecompiled::call(std::shared_ptr<ExecutiveContext> context, bytesConstRef param)
{
    STORAGE_LOG(DEBUG) << "this: " << this << " call TableFactory:" << toHex(param);


    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    STORAGE_LOG(DEBUG) << "func:" << hex << func;

    dev::eth::ContractABI abi;
    bytes out;

    switch (func)
    {
    case c_openDB_string:  // openDB(string)
    case c_openTable_string:
    {  // openTable(string)
        string tableName;
        abi.abiOut(data, tableName);

        STORAGE_LOG(DEBUG) << "DBFactory open table:" << tableName;
        Address address;
        auto table = m_memoryTableFactory->openTable(tableName);
        if (table)
        {
            TablePrecompiled::Ptr tablePrecompiled = make_shared<TablePrecompiled>();
            tablePrecompiled->setTable(table);
            address = context->registerPrecompiled(tablePrecompiled);
        }
        else
        {
            STORAGE_LOG(DEBUG) << "Open new table:" << tableName << " failed.";
        }

        out = abi.abiIn("", address);
        break;
    }
    case c_createTable_string_string_string:
    {  // createTable(string,string,string)
        string tableName;
        string keyField;
        string valueFiled;

        abi.abiOut(data, tableName, keyField, valueFiled);
        vector<string> fieldNameList;
        boost::split(fieldNameList, valueFiled, boost::is_any_of(","));
        for (auto& str : fieldNameList)
            boost::trim(str);
        valueFiled = boost::join(fieldNameList, ",");
        auto table = m_memoryTableFactory->createTable(tableName, keyField, valueFiled);
        // tableName already exist
        unsigned errorCode = 0;
        if (!table == 0u)
        {
            STORAGE_LOG(ERROR) << "table:" << tableName << " conflict.";
            errorCode = 1;
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

h256 TableFactoryPrecompiled::hash()
{
    return m_memoryTableFactory->hash();
}
