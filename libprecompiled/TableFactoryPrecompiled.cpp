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
#include "Common.h"
#include "TablePrecompiled.h"
#include "libstorage/StorageException.h"
#include "libstorage/Table.h"
#include <libblockverifier/ExecutiveContext.h>
#include <libconfig/GlobalConfigure.h>
#include <libdevcrypto/Common.h>
#include <libdevcrypto/Hash.h>
#include <libethcore/ABI.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/throw_exception.hpp>


using namespace dev;
using namespace dev::blockverifier;
using namespace std;
using namespace dev::storage;
using namespace dev::precompiled;

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


PrecompiledExecResult::Ptr TableFactoryPrecompiled::call(ExecutiveContext::Ptr context,
    bytesConstRef param, Address const& origin, Address const& sender)
{
    STORAGE_LOG(TRACE) << LOG_BADGE("TableFactoryPrecompiled") << LOG_DESC("call")
                       << LOG_KV("param", toHex(param));

    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    dev::eth::ContractABI abi;
    auto callResult = m_precompiledExecResultFactory->createPrecompiledResult();
    callResult->gasPricer()->setMemUsed(param.size());

    if (func == name2Selector[TABLE_METHOD_OPT_STR])
    {  // openTable(string)
        string tableName;
        abi.abiOut(data, tableName);
        tableName = precompiled::getTableName(tableName);

        Address address;
        auto table = m_memoryTableFactory->openTable(tableName);
        callResult->gasPricer()->appendOperation(InterfaceOpcode::OpenTable);
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
        callResult->setExecResult(abi.abiIn("", address));
    }
    else if (func == name2Selector[TABLE_METHOD_CRT_STR_STR])
    {  // createTable(string,string,string)
        if (g_BCOSConfig.version() >= V2_3_0 && !checkAuthority(context, origin, sender))
        {
            PRECOMPILED_LOG(ERROR)
                << LOG_BADGE("TableFactoryPrecompiled") << LOG_DESC("permission denied")
                << LOG_KV("origin", origin.hex()) << LOG_KV("contract", sender.hex());
            BOOST_THROW_EXCEPTION(PrecompiledException(
                "Permission denied. " + origin.hex() + " can't call contract " + sender.hex()));
        }
        string tableName;
        string keyField;
        string valueFiled;
        abi.abiOut(data, tableName, keyField, valueFiled);
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TableFactory") << LOG_KV("createTable", tableName)
                               << LOG_KV("keyField", keyField) << LOG_KV("valueFiled", valueFiled);
        vector<string> fieldNameList;
        boost::split(fieldNameList, valueFiled, boost::is_any_of(","));
        boost::trim(keyField);
        if (keyField.size() > (size_t)SYS_TABLE_KEY_FIELD_NAME_MAX_LENGTH)
        {  // mysql TableName and fieldName length limit is 64
            BOOST_THROW_EXCEPTION(StorageException(CODE_TABLE_FILED_LENGTH_OVERFLOW,
                std::string("table field name length overflow ") +
                    std::to_string(SYS_TABLE_KEY_FIELD_NAME_MAX_LENGTH)));
        }
        for (auto& str : fieldNameList)
        {
            boost::trim(str);
            if (str.size() > (size_t)SYS_TABLE_KEY_FIELD_NAME_MAX_LENGTH)
            {  // mysql TableName and fieldName length limit is 64
                BOOST_THROW_EXCEPTION(StorageException(CODE_TABLE_FILED_LENGTH_OVERFLOW,
                    std::string("table field name length overflow ") +
                        std::to_string(SYS_TABLE_KEY_FIELD_NAME_MAX_LENGTH)));
            }
        }

        checkNameValidate(tableName, keyField, fieldNameList);

        valueFiled = boost::join(fieldNameList, ",");
        if (valueFiled.size() > (size_t)SYS_TABLE_VALUE_FIELD_MAX_LENGTH)
        {
            BOOST_THROW_EXCEPTION(StorageException(CODE_TABLE_FILED_TOTALLENGTH_OVERFLOW,
                std::string("total table field name length overflow ") +
                    std::to_string(SYS_TABLE_VALUE_FIELD_MAX_LENGTH)));
        }

        tableName = precompiled::getTableName(tableName);
        if (tableName.size() > (size_t)USER_TABLE_NAME_MAX_LENGTH ||
            (g_BCOSConfig.version() > V2_1_0 &&
                tableName.size() > (size_t)USER_TABLE_NAME_MAX_LENGTH_S))
        {  // mysql TableName and fieldName length limit is 64
           // 2.2.0 user tableName length limit is 50-2=48
            BOOST_THROW_EXCEPTION(StorageException(
                CODE_TABLE_NAME_LENGTH_OVERFLOW, std::string("tableName length overflow ") +
                                                     std::to_string(USER_TABLE_NAME_MAX_LENGTH)));
        }

        int result = 0;

        if (g_BCOSConfig.version() < RC2_VERSION)
        {  // RC1 success result is 1
            result = 1;
        }
        try
        {
            auto table =
                m_memoryTableFactory->createTable(tableName, keyField, valueFiled, true, origin);
            if (!table)
            {  // table already exist
                result = CODE_TABLE_NAME_ALREADY_EXIST;
                /// RC1 table already exist: 0
                if (g_BCOSConfig.version() < RC2_VERSION)
                {
                    result = 0;
                }
            }
            else
            {
                callResult->gasPricer()->appendOperation(InterfaceOpcode::CreateTable);
            }
        }
        catch (dev::storage::StorageException& e)
        {
            STORAGE_LOG(ERROR) << "Create table failed: " << boost::diagnostic_information(e);
            result = e.errorCode();
        }
        getErrorCodeOut(callResult->mutableExecResult(), result);
    }
    else
    {
        STORAGE_LOG(ERROR) << LOG_BADGE("TableFactoryPrecompiled")
                           << LOG_DESC("call undefined function!");
    }
    return callResult;
}

h256 TableFactoryPrecompiled::hash()
{
    return m_memoryTableFactory->hash();
}
