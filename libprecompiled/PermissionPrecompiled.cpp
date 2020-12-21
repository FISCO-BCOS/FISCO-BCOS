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
#include "libstoragestate/StorageState.h"
#include <json/json.h>
#include <libblockverifier/ExecutiveContext.h>
#include <libprecompiled/TableFactoryPrecompiled.h>
#include <libprotocol/ContractABICodec.h>
#include <boost/lexical_cast.hpp>

using namespace std;
using namespace bcos;
using namespace bcos::blockverifier;
using namespace bcos::storage;
using namespace bcos::precompiled;

const char* const AUP_METHOD_INS = "insert(string,string)";
const char* const AUP_METHOD_REM = "remove(string,string)";
const char* const AUP_METHOD_QUE = "queryByName(string)";
const char* const AUP_METHOD_GRANT_WRITE_CONTRACT = "grantWrite(address,address)";
const char* const AUP_METHOD_REVOKE_WRITE_CONTRACT = "revokeWrite(address,address)";
const char* const AUP_METHOD_QUERY_CONTRACT = "queryPermission(address)";


PermissionPrecompiled::PermissionPrecompiled()
{
    name2Selector[AUP_METHOD_INS] = getFuncSelector(AUP_METHOD_INS);
    name2Selector[AUP_METHOD_REM] = getFuncSelector(AUP_METHOD_REM);
    name2Selector[AUP_METHOD_QUE] = getFuncSelector(AUP_METHOD_QUE);
    name2Selector[AUP_METHOD_GRANT_WRITE_CONTRACT] =
        getFuncSelector(AUP_METHOD_GRANT_WRITE_CONTRACT);
    name2Selector[AUP_METHOD_REVOKE_WRITE_CONTRACT] =
        getFuncSelector(AUP_METHOD_REVOKE_WRITE_CONTRACT);
    name2Selector[AUP_METHOD_QUERY_CONTRACT] = getFuncSelector(AUP_METHOD_QUERY_CONTRACT);
}

std::string PermissionPrecompiled::toString()
{
    return "Permission";
}

PrecompiledExecResult::Ptr PermissionPrecompiled::call(
    ExecutiveContext::Ptr context, bytesConstRef param, Address const& origin, Address const&)
{
    // parse function name
    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    bcos::protocol::ContractABICodec abi;
    auto callResult = m_precompiledExecResultFactory->createPrecompiledResult();
    int result = 0;
    if (func == name2Selector[AUP_METHOD_INS])
    {  // FIXME: modify insert(string,string) ==> insert(string,address)
        // insert(string tableName,string addr)
        std::string tableName, addr;
        abi.abiOut(data, tableName, addr);
        boost::to_lower(addr);
        do
        {
            Table::Ptr table = openTable(context, SYS_ACCESS_TABLE);
            if (tableName == SYS_ACCESS_TABLE)
            {
                result = CODE_COMMITTEE_PERMISSION;
                PRECOMPILED_LOG(WARNING)
                    << LOG_BADGE("PermissionPrecompiled")
                    << LOG_DESC("Committee permission controlled by ChainGovernancePrecompiled")
                    << LOG_KV("return", result);
                break;
            }
            auto condition = table->newCondition();
            condition->EQ(SYS_AC_ADDRESS, addr);
            auto entries = table->select(SYS_ACCESS_TABLE, condition);
            if (entries->size() != 0u && (tableName == SYS_TABLES || tableName == SYS_CNS))
            {  // committee
                result = CODE_COMMITTEE_MEMBER_CANNOT_BE_OPERATOR;

                PRECOMPILED_LOG(WARNING)
                    << LOG_BADGE("PermissionPrecompiled")
                    << LOG_DESC("committee member should not have operator permission")
                    << LOG_KV("account", addr)
                    << LOG_KV("return", CODE_COMMITTEE_MEMBER_CANNOT_BE_OPERATOR);
                break;
            }
            addPrefixToUserTable(tableName);

            condition = table->newCondition();
            condition->EQ(SYS_AC_ADDRESS, addr);
            entries = table->select(tableName, condition);
            if (entries->size() != 0u)
            {
                PRECOMPILED_LOG(WARNING) << LOG_BADGE("PermissionPrecompiled")
                                         << LOG_DESC("tableName and address exist");
                result = CODE_TABLE_AND_ADDRESS_EXIST;
                break;
            }
            if (tableName.size() > USER_TABLE_NAME_MAX_LENGTH)
            {
                PRECOMPILED_LOG(ERROR)
                    << LOG_BADGE("PermissionPrecompiled") << LOG_DESC("tableName overflow")
                    << LOG_KV("tableName", tableName);
                result = CODE_TABLE_NAME_OVERFLOW;
                break;
            }

            try
            {
                Address address(addr);
                (void)address;
            }
            catch (...)
            {
                PRECOMPILED_LOG(ERROR) << LOG_BADGE("PermissionPrecompiled")
                                       << LOG_DESC("address invalid") << LOG_KV("account", addr);
                result = CODE_ADDRESS_INVALID;
                break;
            }

            auto entry = table->newEntry();
            entry->setField(SYS_AC_TABLE_NAME, tableName);
            entry->setField(SYS_AC_ADDRESS, addr);
            entry->setField(SYS_AC_ENABLENUM,
                boost::lexical_cast<std::string>(context->blockInfo().number + 1));
            int count = table->insert(tableName, entry, std::make_shared<AccessOptions>(origin));
            result = count;
            PRECOMPILED_LOG(INFO) << LOG_BADGE("PermissionPrecompiled") << LOG_DESC("insert func")
                                  << LOG_KV("tableName", tableName) << LOG_KV("address", addr)
                                  << LOG_KV("return", count);
        } while (0);

        getErrorCodeOut(callResult->mutableExecResult(), result);
    }
    else if (func == name2Selector[AUP_METHOD_REM])
    {  // remove(string tableName,string addr)
        std::string tableName, addr;
        abi.abiOut(data, tableName, addr);
        boost::to_lower(addr);
        int result = 0;
        Table::Ptr table = openTable(context, SYS_ACCESS_TABLE);
        auto condition = table->newCondition();
        condition->EQ(SYS_AC_ADDRESS, addr);
        auto entries = table->select(SYS_ACCESS_TABLE, condition);
        if (tableName == SYS_ACCESS_TABLE)
        {
            result = CODE_COMMITTEE_PERMISSION;
            PRECOMPILED_LOG(WARNING)
                << LOG_BADGE("PermissionPrecompiled")
                << LOG_DESC("Committee permission controlled by ChainGovernancePrecompiled")
                << LOG_KV("return", result);
        }
        else if (entries->size() != 0u && (tableName == SYS_CONSENSUS || tableName == SYS_CONFIG))
        {
            result = storage::CODE_NO_AUTHORIZED;
            PRECOMPILED_LOG(WARNING)
                << LOG_BADGE("PermissionPrecompiled")
                << LOG_DESC("Committee permission should not be removed by PermissionPrecompiled")
                << LOG_KV("return", result);
        }
        else
        {
            addPrefixToUserTable(tableName);
            result = revokeTablePermission(context, tableName, addr, origin);
        }
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("PermissionPrecompiled") << LOG_DESC("remove")
                               << LOG_KV("tableName", tableName) << LOG_KV("account", addr)
                               << LOG_KV("ret", result);
        getErrorCodeOut(callResult->mutableExecResult(), result);
    }
    else if (func == name2Selector[AUP_METHOD_QUE])
    {  // queryByName(string table_name)
        std::string tableName;
        abi.abiOut(data, tableName);
        addPrefixToUserTable(tableName);

        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("PermissionPrecompiled") << LOG_DESC("queryByName func")
                               << LOG_KV("tableName", tableName);

        auto result = queryPermission(context, tableName);
        callResult->setExecResult(abi.abiIn("", result));
    }
    else if (func == name2Selector[AUP_METHOD_GRANT_WRITE_CONTRACT])
    {  // grantWrite(address,address)
        Address contractAddress, user;
        abi.abiOut(data, contractAddress, user);
        string addr = user.hexPrefixed();
        string tableName = precompiled::getContractTableName(contractAddress);
        PRECOMPILED_LOG(INFO) << LOG_BADGE("PermissionPrecompiled") << LOG_DESC("grantWrite")
                              << LOG_KV("tableName", tableName) << LOG_KV("user", addr);
        do
        {
            Table::Ptr table = openTable(context, SYS_ACCESS_TABLE);
            auto condition = table->newCondition();
            condition->EQ(SYS_AC_ADDRESS, addr);
            auto entries = table->select(tableName, condition);
            if (entries->size() != 0u)
            {
                PRECOMPILED_LOG(WARNING) << LOG_BADGE("PermissionPrecompiled")
                                         << LOG_DESC("tableName and address exist");
                result = CODE_TABLE_AND_ADDRESS_EXIST;
                break;
            }
            if (tableName.size() > USER_TABLE_NAME_MAX_LENGTH)
            {
                PRECOMPILED_LOG(ERROR)
                    << LOG_BADGE("PermissionPrecompiled") << LOG_DESC("tableName overflow")
                    << LOG_KV("tableName", tableName);
                result = CODE_TABLE_NAME_OVERFLOW;
                break;
            }
            if (!openTable(context, tableName))
            {
                PRECOMPILED_LOG(ERROR)
                    << LOG_BADGE("PermissionPrecompiled") << LOG_DESC("contract not exist")
                    << LOG_KV("contract", tableName);
                result = CODE_CONTRACT_NOT_EXIST;
                break;
            }
            if (!checkPermission(context, tableName, origin))
            {
                PRECOMPILED_LOG(INFO) << LOG_BADGE("PermissionPrecompiled")
                                      << LOG_DESC("only deployer of contract can grantWrite")
                                      << LOG_KV("tableName", tableName)
                                      << LOG_KV("origin", origin.hex()) << LOG_KV("user", addr);
                result = storage::CODE_NO_AUTHORIZED;
                break;
            }
            auto entry = table->newEntry();
            entry->setField(SYS_AC_TABLE_NAME, tableName);
            entry->setField(SYS_AC_ADDRESS, addr);
            entry->setField(SYS_AC_ENABLENUM,
                boost::lexical_cast<std::string>(context->blockInfo().number + 1));
            auto accessOption = std::make_shared<AccessOptions>(origin, false);

            int count = table->insert(tableName, entry, accessOption);
            result = count;
            PRECOMPILED_LOG(INFO) << LOG_BADGE("PermissionPrecompiled grantWrite")
                                  << LOG_KV("return", result);
        } while (0);

        getErrorCodeOut(callResult->mutableExecResult(), result);
    }
    else if (func == name2Selector[AUP_METHOD_REVOKE_WRITE_CONTRACT])
    {  // revokeWrite(address,address)
        Address contractAddress, user;
        abi.abiOut(data, contractAddress, user);
        string addr = user.hexPrefixed();
        string tableName = precompiled::getContractTableName(contractAddress);
        int result = revokeContractTablePermission(context, tableName, addr, origin);
        callResult->setExecResult(abi.abiIn("", s256(result)));
    }
    else if (func == name2Selector[AUP_METHOD_QUERY_CONTRACT])
    {  // queryPermission(address)
        Address contractAddress;
        abi.abiOut(data, contractAddress);
        string tableName = precompiled::getContractTableName(contractAddress);
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("PermissionPrecompiled") << LOG_DESC("queryPermission")
                               << LOG_KV("tableName", tableName);
        auto result = queryPermission(context, tableName);
        callResult->setExecResult(abi.abiIn("", result));
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("PermissionPrecompiled")
                               << LOG_DESC("call undefined function") << LOG_KV("func", func);
    }
    return callResult;
}

string PermissionPrecompiled::queryPermission(
    std::shared_ptr<bcos::blockverifier::ExecutiveContext> context, const string& tableName)
{
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
    return fastWriter.write(AuthorityInfos);
}

int PermissionPrecompiled::revokeWritePermission(
    std::shared_ptr<bcos::blockverifier::ExecutiveContext> context, const string& tableName,
    const string& user, Address const& origin, bool _isContractTable)
{
    int result;
    Table::Ptr table = openTable(context, SYS_ACCESS_TABLE);
    auto condition = table->newCondition();
    condition->EQ(SYS_AC_ADDRESS, user);
    auto entries = table->select(tableName, condition);
    if (entries->size() == 0u)
    {
        PRECOMPILED_LOG(WARNING) << LOG_BADGE("PermissionPrecompiled")
                                 << LOG_DESC("tableName and address does not exist");
        return result = CODE_TABLE_AND_ADDRESS_NOT_EXIST;
    }
    if (_isContractTable && !checkPermission(context, tableName, origin))
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("PermissionPrecompiled")
                              << LOG_DESC("only deployer of contract can revokeWrite")
                              << LOG_KV("tableName", tableName) << LOG_KV("origin", origin.hex())
                              << LOG_KV("user", user);
        return storage::CODE_NO_AUTHORIZED;
    }
    auto accessOption = std::make_shared<AccessOptions>(origin);
    if (_isContractTable)
    {
        accessOption = std::make_shared<AccessOptions>(origin, false);
    }
    result = table->remove(tableName, condition, accessOption);
    PRECOMPILED_LOG(INFO) << LOG_BADGE("PermissionPrecompiled") << LOG_DESC("revokeWrite")
                          << LOG_KV("tableName", tableName) << LOG_KV("user", user)
                          << LOG_KV("return", result);
    return result;
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

bool PermissionPrecompiled::checkPermission(
    ExecutiveContext::Ptr context, std::string const& tableName, Address const& origin)
{
    Table::Ptr table = openTable(context, tableName);
    if (table)
    {
        auto entries = table->select(storagestate::ACCOUNT_AUTHORITY, table->newCondition());
        if (entries->size() > 0)
        {  // the key of authority would be set when support version >= 2.3.0
            for (size_t i = 0; i < entries->size(); i++)
            {
                std::string authority = entries->get(i)->getField(storagestate::STORAGE_VALUE);
                if (origin.hex() == authority)
                {
                    return true;
                }
            }
        }
    }
    return false;
}
