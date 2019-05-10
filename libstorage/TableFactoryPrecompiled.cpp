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
/** @file TableFactoryPrecompiled.h
 *  @author ancelmo
 *  @date 20180921
 */
#include "TableFactoryPrecompiled.h"
#include "StorageException.h"
#include "Table.h"
#include "TablePrecompiled.h"
#include <libblockverifier/ExecutiveContext.h>
#include <libconfig/GlobalConfigure.h>
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
using namespace dev::storage;

const char* const TABLE_METHOD_OPT_STR = "openTable(string)";
const char* const TABLE_METHOD_CRT_STR_STR = "createTable(string,string,string)";

TableFactoryPrecompiled::TableFactoryPrecompiled()
{
    name2Selector[TABLE_METHOD_OPT_STR] = getFuncSelector(TABLE_METHOD_OPT_STR);
    name2Selector[TABLE_METHOD_CRT_STR_STR] = getFuncSelector(TABLE_METHOD_CRT_STR_STR);
}

std::string TableFactoryPrecompiled::toString()
{
    return "TableFactory";
}

bytes TableFactoryPrecompiled::call(
    ExecutiveContext::Ptr context, bytesConstRef param, Address const& origin)
{
    STORAGE_LOG(TRACE) << LOG_BADGE("TableFactoryPrecompiled") << LOG_DESC("call")
                       << LOG_KV("param", toHex(param));

    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    dev::eth::ContractABI abi;
    bytes out;

    if (func == name2Selector[TABLE_METHOD_OPT_STR])
    {  // openTable(string)
        string tableName;
        abi.abiOut(data, tableName);
        tableName = storage::USER_TABLE_PREFIX + tableName;
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
            STORAGE_LOG(WARNING) << LOG_BADGE("TableFactoryPrecompiled")
                                 << LOG_DESC("Open new table failed")
                                 << LOG_KV("table name", tableName);
        }

        out = abi.abiIn("", address);
    }
    else if (func == name2Selector[TABLE_METHOD_CRT_STR_STR])
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
        tableName = storage::USER_TABLE_PREFIX + tableName;

        int result = 0;

        /// RC1 success result is 1
        if (g_BCOSConfig.version() < RC2_VERSION)
        {
            result = 1;
        }
        try
        {
            /// table already exist
            auto table =
                m_memoryTableFactory->createTable(tableName, keyField, valueFiled, true, origin);
            if (!table)
            {
                result = CODE_TABLE_NAME_ALREADY_EXIST;
                /// RC1 table already exist: 0
                if (g_BCOSConfig.version() < RC2_VERSION)
                {
                    result = 0;
                }
            }
        }
        catch (dev::storage::StorageException& e)
        {
            STORAGE_LOG(ERROR) << "Create table failed: " << boost::diagnostic_information(e);
            result = e.errorCode();
        }

        if (result == 1)
        {
            out = abi.abiIn("", s256(result));
        }
        else if (g_BCOSConfig.version() < RC2_VERSION)
        {
            out = abi.abiIn("", -result);
        }
        else if (g_BCOSConfig.version() == RC2_VERSION)
        {
            out = abi.abiIn("", s256(-result));
        }
        else
        {
            out = abi.abiIn("", s256(result));
        }
    }
    else
    {
        STORAGE_LOG(ERROR) << LOG_BADGE("TableFactoryPrecompiled")
                           << LOG_DESC("call undefined function!");
    }
    return out;
}

h256 TableFactoryPrecompiled::hash()
{
    return m_memoryTableFactory->hash();
}
