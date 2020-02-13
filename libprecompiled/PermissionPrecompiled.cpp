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
/** @file PermissionPrecompiled.h
 *  @author caryliao
 *  @date 20181205
 */
#include "PermissionPrecompiled.h"
#include "libstorage/Table.h"
#include <json/json.h>
#include <libblockverifier/ExecutiveContext.h>
#include <libethcore/ABI.h>
#include <libprecompiled/TableFactoryPrecompiled.h>
#include <boost/lexical_cast.hpp>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::storage;
using namespace dev::precompiled;

const char* const AUP_METHOD_INS = "insert(string,string)";
const char* const AUP_METHOD_REM = "remove(string,string)";
const char* const AUP_METHOD_QUE = "queryByName(string)";


PermissionPrecompiled::PermissionPrecompiled()
{
    name2Selector[AUP_METHOD_INS] = getFuncSelector(AUP_METHOD_INS);
    name2Selector[AUP_METHOD_REM] = getFuncSelector(AUP_METHOD_REM);
    name2Selector[AUP_METHOD_QUE] = getFuncSelector(AUP_METHOD_QUE);
}

std::string PermissionPrecompiled::toString()
{
    return "Authority";
}

bytes PermissionPrecompiled::call(
    ExecutiveContext::Ptr context, bytesConstRef param, Address const& origin)
{
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("PermissionPrecompiled") << LOG_DESC("call")
                           << LOG_KV("param", toHex(param));

    // parse function name
    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    dev::eth::ContractABI abi;
    bytes out;
    int result = 0;
    if (func == name2Selector[AUP_METHOD_INS])
    {  // FIXME: modify insert(string,string) ==> insert(string,address)
        // insert(string tableName,string addr)
        std::string tableName, addr;
        abi.abiOut(data, tableName, addr);
        addPrefixToUserTable(tableName);
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("PermissionPrecompiled") << LOG_DESC("insert func")
                               << LOG_KV("tableName", tableName) << LOG_KV("address", addr);
        bool isValidAddress = true;
        try
        {
            Address address(addr);
            (void)address;
        }
        catch (...)
        {
            isValidAddress = false;
        }

        Table::Ptr table = openTable(context, SYS_ACCESS_TABLE);

        auto condition = table->newCondition();
        condition->EQ(SYS_AC_ADDRESS, addr);
        auto entries = table->select(tableName, condition);
        if (entries->size() != 0u)
        {
            PRECOMPILED_LOG(WARNING)
                << LOG_BADGE("PermissionPrecompiled") << LOG_DESC("tableName and address exist");
            result = CODE_TABLE_AND_ADDRESS_EXIST;
        }
        else if (tableName.size() > USER_TABLE_NAME_MAX_LENGTH)
        {
            PRECOMPILED_LOG(ERROR)
                << LOG_BADGE("PermissionPrecompiled") << LOG_DESC("tableName overflow")
                << LOG_KV("tableName", tableName);
            result = CODE_TABLE_NAME_OVERFLOW;
        }
        else if (!isValidAddress)
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("PermissionPrecompiled")
                                   << LOG_DESC("address invalid") << LOG_KV("address", addr);
            result = CODE_ADDRESS_INVALID;
        }
        else
        {
            auto entry = table->newEntry();
            entry->setField(SYS_AC_TABLE_NAME, tableName);
            entry->setField(SYS_AC_ADDRESS, addr);
            entry->setField(SYS_AC_ENABLENUM,
                boost::lexical_cast<std::string>(context->blockInfo().number + 1));
            int count = table->insert(tableName, entry, std::make_shared<AccessOptions>(origin));
            result = count;
            PRECOMPILED_LOG(DEBUG)
                << LOG_BADGE("PermissionPrecompiled")
                << LOG_KV("insert_success", (count == storage::CODE_NO_AUTHORIZED ? false : true));
        }
        getErrorCodeOut(out, result);
    }
    else if (func == name2Selector[AUP_METHOD_REM])
    {
        // remove(string tableName,string addr)
        std::string tableName, addr;
        abi.abiOut(data, tableName, addr);
        addPrefixToUserTable(tableName);

        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("PermissionPrecompiled") << LOG_DESC("remove func")
                               << LOG_KV("tableName", tableName) << LOG_KV("address", addr);

        Table::Ptr table = openTable(context, SYS_ACCESS_TABLE);

        auto condition = table->newCondition();
        condition->EQ(SYS_AC_ADDRESS, addr);
        auto entries = table->select(tableName, condition);
        if (entries->size() == 0u)
        {
            PRECOMPILED_LOG(WARNING) << LOG_BADGE("PermissionPrecompiled")
                                     << LOG_DESC("tableName and address does not exist");
            result = CODE_TABLE_AND_ADDRESS_NOT_EXIST;
        }
        else
        {
            int count =
                table->remove(tableName, condition, std::make_shared<AccessOptions>(origin));
            result = count;
            PRECOMPILED_LOG(DEBUG)
                << LOG_BADGE("PermissionPrecompiled")
                << LOG_KV("remove_success", (count == storage::CODE_NO_AUTHORIZED ? false : true));
        }
        getErrorCodeOut(out, result);
    }
    else if (func == name2Selector[AUP_METHOD_QUE])
    {
        // queryByName(string table_name)
        std::string tableName;
        abi.abiOut(data, tableName);
        addPrefixToUserTable(tableName);

        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("PermissionPrecompiled") << LOG_DESC("queryByName func")
                               << LOG_KV("tableName", tableName);

        Table::Ptr table = openTable(context, SYS_ACCESS_TABLE);

        auto condition = table->newCondition();
        auto entries = table->select(tableName, condition);
        Json::Value AuthorityInfos(Json::arrayValue);
        if (entries)
        {
            for (size_t i = 0; i < entries->size(); i++)
            {
                auto entry = entries->get(i);
                if (!entry)
                    continue;
                Json::Value AuthorityInfo;
                AuthorityInfo[SYS_AC_TABLE_NAME] = tableName;
                AuthorityInfo[SYS_AC_ADDRESS] = entry->getField(SYS_AC_ADDRESS);
                AuthorityInfo[SYS_AC_ENABLENUM] = entry->getField(SYS_AC_ENABLENUM);
                AuthorityInfos.append(AuthorityInfo);
            }
        }
        Json::FastWriter fastWriter;
        std::string str = fastWriter.write(AuthorityInfos);

        out = abi.abiIn("", str);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("PermissionPrecompiled")
                               << LOG_DESC("call undefined function") << LOG_KV("func", func);
    }
    return out;
}

void PermissionPrecompiled::addPrefixToUserTable(std::string& table_name)
{
    if (table_name == SYS_ACCESS_TABLE || table_name == SYS_CONSENSUS || table_name == SYS_TABLES ||
        table_name == SYS_CNS || table_name == SYS_CONFIG)
    {
        return;
    }
    else
    {
        table_name = precompiled::getTableName(table_name);
    }
}
