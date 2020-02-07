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
/** @file KVTableFactoryPrecompiled.h
 *  @author xingqiangbai
 *  @date 20200206
 */
#include "KVTableFactoryPrecompiled.h"
#include "Common.h"
#include "KVTablePrecompiled.h"
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

const char* const KVTABLE_FACTORY_METHOD_OPEN_TABLE = "openTable(string)";
const char* const KVTABLE_FACTORY_METHOD_CREATE_TABLE = "createTable(string,string,string)";

KVTableFactoryPrecompiled::KVTableFactoryPrecompiled()
{
    name2Selector[KVTABLE_FACTORY_METHOD_OPEN_TABLE] =
        getFuncSelector(KVTABLE_FACTORY_METHOD_OPEN_TABLE);
    name2Selector[KVTABLE_FACTORY_METHOD_CREATE_TABLE] =
        getFuncSelector(KVTABLE_FACTORY_METHOD_CREATE_TABLE);
}

std::string KVTableFactoryPrecompiled::toString()
{
    return "KVTableFactory";
}

bytes KVTableFactoryPrecompiled::call(
    ExecutiveContext::Ptr context, bytesConstRef param, Address const& origin)
{
    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);
    STORAGE_LOG(DEBUG) << LOG_BADGE("KVTableFactory") << LOG_DESC("call") << LOG_KV("func", func);

    dev::eth::ContractABI abi;
    bytes out;

    if (func == name2Selector[KVTABLE_FACTORY_METHOD_OPEN_TABLE])
    {  // openTable(string)
        string tableName;
        abi.abiOut(data, tableName);
        tableName = precompiled::getTableName(tableName);
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("KVTableFactory") << LOG_KV("openTable", tableName);
        Address address;
        auto table = m_memoryTableFactory->openTable(tableName);
        if (table)
        {
            auto kvTablePrecompiled = make_shared<KVTablePrecompiled>();
            kvTablePrecompiled->setTable(table);
            address = context->registerPrecompiled(kvTablePrecompiled);
        }
        else
        {
            STORAGE_LOG(WARNING) << LOG_BADGE("KVTableFactoryPrecompiled")
                                 << LOG_DESC("Open new table failed")
                                 << LOG_KV("table name", tableName);
        }

        out = abi.abiIn("", address);
    }
    else if (func == name2Selector[KVTABLE_FACTORY_METHOD_CREATE_TABLE])
    {  // createTable(string,string,string)
        string tableName;
        string keyField;
        string valueFiled;

        abi.abiOut(data, tableName, keyField, valueFiled);
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("KVTableFactory") << LOG_KV("createTable", tableName)
                               << LOG_KV("keyField", keyField) << LOG_KV("valueFiled", valueFiled);

        vector<string> fieldNameList;
        boost::split(fieldNameList, valueFiled, boost::is_any_of(","));
        boost::trim(keyField);
        if (keyField.size() > (size_t)SYS_TABLE_KEY_FIELD_NAME_MAX_LENGTH)
        {  // mysql TableName and fieldName length limit is 64
            BOOST_THROW_EXCEPTION(
                PrecompiledException(std::string("table field name length overflow ") +
                                     std::to_string(SYS_TABLE_KEY_FIELD_NAME_MAX_LENGTH)));
        }
        for (auto& str : fieldNameList)
        {
            boost::trim(str);
            if (str.size() > (size_t)SYS_TABLE_KEY_FIELD_NAME_MAX_LENGTH)
            {  // mysql TableName and fieldName length limit is 64
                BOOST_THROW_EXCEPTION(
                    PrecompiledException(std::string("table field name length overflow ") +
                                         std::to_string(SYS_TABLE_KEY_FIELD_NAME_MAX_LENGTH)));
            }
        }

        checkNameValidate(tableName, keyField, fieldNameList, false);

        valueFiled = boost::join(fieldNameList, ",");
        if (valueFiled.size() > (size_t)SYS_TABLE_VALUE_FIELD_MAX_LENGTH)
        {
            BOOST_THROW_EXCEPTION(
                PrecompiledException(std::string("total table field name length overflow ") +
                                     std::to_string(SYS_TABLE_VALUE_FIELD_MAX_LENGTH)));
        }

        tableName = precompiled::getTableName(tableName);
        if (tableName.size() > (size_t)USER_TABLE_NAME_MAX_LENGTH_S)
        {  // mysql TableName and fieldName length limit is 64
           // 2.2.0 user tableName length limit is 50-2=48
            BOOST_THROW_EXCEPTION(PrecompiledException(std::string("tableName length overflow ") +
                                                       std::to_string(USER_TABLE_NAME_MAX_LENGTH)));
        }

        int result = 0;

        try
        {
            auto table =
                m_memoryTableFactory->createTable(tableName, keyField, valueFiled, true, origin);
            if (!table)
            {  // table already exist
                result = CODE_TABLE_NAME_ALREADY_EXIST;
            }
        }
        catch (dev::storage::StorageException& e)
        {
            STORAGE_LOG(ERROR) << "Create table failed: " << boost::diagnostic_information(e);
            result = e.errorCode();
        }
        getErrorCodeOut(out, result);
    }
    else
    {
        STORAGE_LOG(ERROR) << LOG_BADGE("KVTableFactoryPrecompiled")
                           << LOG_DESC("call undefined function!");
    }
    return out;
}

h256 KVTableFactoryPrecompiled::hash()
{
    return m_memoryTableFactory->hash();
}
