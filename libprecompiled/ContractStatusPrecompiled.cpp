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
/** @file ContractStatusPrecompiled.cpp
 *  @author chaychen
 *  @date 20190106
 */
#include "ContractStatusPrecompiled.h"

#include "libprecompiled/EntriesPrecompiled.h"
#include "libprecompiled/TableFactoryPrecompiled.h"
#include "libstoragestate/StorageState.h"
#include <libethcore/ABI.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/lexical_cast.hpp>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::storage;
using namespace dev::precompiled;

// precompiled contract function
/*
contract ContractStatusPrecompiled {
    function freeze(address addr) public returns(int);
    function unfreeze(address addr) public returns(int);
    function grantManager(address contractAddr, address userAddr) public returns(int);
    function getStatus(address addr) public constant returns(int,string);
    function listManager(address addr) public constant returns(int,address[]);
}
*/

const char* const METHOD_FREEZE_STR = "freeze(address)";
const char* const METHOD_UNFREEZE_STR = "unfreeze(address)";
const char* const METHOD_GRANT_STR = "grantManager(address,address)";
const char* const METHOD_QUERY_STR = "getStatus(address)";
const char* const METHOD_QUERY_AUTHORITY = "listManager(address)";

// contract state
// available,frozen

// state-transition matrix
/*
            available frozen
   freeze   frozen    ×
   unfreeze ×         available
*/

ContractStatusPrecompiled::ContractStatusPrecompiled()
{
    name2Selector[METHOD_FREEZE_STR] = getFuncSelector(METHOD_FREEZE_STR);
    name2Selector[METHOD_UNFREEZE_STR] = getFuncSelector(METHOD_UNFREEZE_STR);
    name2Selector[METHOD_GRANT_STR] = getFuncSelector(METHOD_GRANT_STR);
    name2Selector[METHOD_QUERY_STR] = getFuncSelector(METHOD_QUERY_STR);
    name2Selector[METHOD_QUERY_AUTHORITY] = getFuncSelector(METHOD_QUERY_AUTHORITY);
}

bool ContractStatusPrecompiled::checkPermission(
    ExecutiveContext::Ptr context, std::string const& tableName, Address const& origin)
{
    Table::Ptr table = openTable(context, tableName);
    if (table)
    {
        auto entries = table->select(storagestate::ACCOUNT_AUTHORITY, table->newCondition());
        if (entries->size() > 0)
        {
            // the key of authority would be set when support version >= 2.3.0
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

ContractStatus ContractStatusPrecompiled::getContractStatus(
    ExecutiveContext::Ptr context, std::string const& tableName)
{
    ContractStatus status = ContractStatus::Invalid;

    Table::Ptr table = openTable(context, tableName);
    if (table)
    {
        auto entries = table->select(storagestate::ACCOUNT_FROZEN, table->newCondition());
        if (entries->size() > 0 &&
            STATUS_TRUE == entries->get(0)->getField(storagestate::STORAGE_VALUE))
        {
            status = ContractStatus::Frozen;
        }
        else
        {
            status = ContractStatus::Available;
        }
    }
    else
    {  // valid address but nonexistent
        status = ContractStatus::Nonexistent;
    }

    if (ContractStatus::Invalid == status)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractStatusPrecompiled")
                               << LOG_DESC("getContractStatus error")
                               << LOG_KV("table name", tableName);
    }
    return status;
}

int ContractStatusPrecompiled::updateFrozenStatus(ExecutiveContext::Ptr context,
    std::string const& tableName, std::string const& frozen, Address const& origin)
{
    int result = 0;

    Table::Ptr table = openTable(context, tableName);
    if (table)
    {
        auto entries = table->select(storagestate::ACCOUNT_FROZEN, table->newCondition());
        auto entry = table->newEntry();
        entry->setField(storagestate::STORAGE_VALUE, frozen);
        if (entries->size() != 0u)
        {
            result = table->update(storagestate::ACCOUNT_FROZEN, entry, table->newCondition(),
                std::make_shared<AccessOptions>(origin, false));
        }
        else
        {
            result = table->insert(storagestate::ACCOUNT_FROZEN, entry,
                std::make_shared<AccessOptions>(origin, false));
        }

        if (result == storage::CODE_NO_AUTHORIZED)
        {
            PRECOMPILED_LOG(DEBUG)
                << LOG_BADGE("ContractStatusPrecompiled") << LOG_DESC("permission denied");
        }
    }

    return result;
}

void ContractStatusPrecompiled::freeze(
    ExecutiveContext::Ptr context, bytesConstRef data, Address const& origin, bytes& out)
{
    dev::eth::ContractABI abi;
    Address contractAddress;
    abi.abiOut(data, contractAddress);
    int result = 0;

    std::string tableName = precompiled::getContractTableName(contractAddress);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractStatusPrecompiled")
                           << LOG_KV("freeze contract", tableName);
    // administrative authority judgment precedes status judgment
    if (checkPermission(context, tableName, origin))
    {
        ContractStatus status = getContractStatus(context, tableName);
        if (ContractStatus::Frozen == status)
        {
            result = CODE_INVALID_CONTRACT_FEOZEN;
        }
        else if (ContractStatus::Nonexistent == status)
        {
            result = CODE_TABLE_AND_ADDRESS_NOT_EXIST;
        }
        else
        {
            result = updateFrozenStatus(context, tableName, STATUS_TRUE, origin);
        }
    }
    else
    {
        result = storage::CODE_NO_AUTHORIZED;
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractStatusPrecompiled")
                               << LOG_DESC("permission denied");
    }
    getErrorCodeOut(out, result);
}

void ContractStatusPrecompiled::unfreeze(
    ExecutiveContext::Ptr context, bytesConstRef data, Address const& origin, bytes& out)
{
    dev::eth::ContractABI abi;
    Address contractAddress;
    abi.abiOut(data, contractAddress);
    int result = 0;


    std::string tableName = precompiled::getContractTableName(contractAddress);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractStatusPrecompiled")
                           << LOG_KV("unfreeze contract", tableName);
    // administrative authority judgment precedes status judgment
    if (checkPermission(context, tableName, origin))
    {
        ContractStatus status = getContractStatus(context, tableName);
        if (ContractStatus::Available == status)
        {
            result = CODE_INVALID_CONTRACT_AVAILABLE;
        }
        else if (ContractStatus::Nonexistent == status)
        {
            result = CODE_TABLE_AND_ADDRESS_NOT_EXIST;
        }
        else
        {
            result = updateFrozenStatus(context, tableName, STATUS_FALSE, origin);
        }
    }
    else
    {
        result = storage::CODE_NO_AUTHORIZED;
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractStatusPrecompiled")
                               << LOG_DESC("permission denied");
    }
    getErrorCodeOut(out, result);
}

void ContractStatusPrecompiled::grantManager(
    ExecutiveContext::Ptr context, bytesConstRef data, Address const& origin, bytes& out)
{
    dev::eth::ContractABI abi;
    Address contractAddress;
    Address userAddress;
    abi.abiOut(data, contractAddress, userAddress);
    int result = 0;

    std::string tableName = precompiled::getContractTableName(contractAddress);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractStatusPrecompiled")
                           << LOG_DESC("grant authorization") << LOG_KV("table", tableName)
                           << LOG_KV("user", userAddress);
    // administrative authority judgment precedes status judgment
    if (checkPermission(context, tableName, origin))
    {
        ContractStatus status = getContractStatus(context, tableName);
        if (ContractStatus::Nonexistent == status)
        {
            result = CODE_TABLE_AND_ADDRESS_NOT_EXIST;
        }
        else
        {
            bool flag = false;
            Table::Ptr table = openTable(context, tableName);
            auto entries = table->select(storagestate::ACCOUNT_AUTHORITY, table->newCondition());
            for (size_t i = 0; i < entries->size(); i++)
            {
                std::string authority = entries->get(i)->getField(storagestate::STORAGE_VALUE);
                if (userAddress.hex() == authority)
                {
                    flag = true;
                    break;
                }
            }
            if (flag == false)
            {
                auto entry = table->newEntry();
                entry->setField(storagestate::STORAGE_KEY, storagestate::ACCOUNT_AUTHORITY);
                entry->setField(storagestate::STORAGE_VALUE, userAddress.hex());
                result = table->insert(storagestate::ACCOUNT_AUTHORITY, entry,
                    std::make_shared<AccessOptions>(origin, false));
            }
            else
            {
                result = CODE_INVALID_CONTRACT_REPEAT_AUTHORIZATION;
            }
        }
    }
    else
    {
        result = storage::CODE_NO_AUTHORIZED;
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractStatusPrecompiled")
                               << LOG_DESC("permission denied");
    }
    getErrorCodeOut(out, result);
}

void ContractStatusPrecompiled::getStatus(
    ExecutiveContext::Ptr context, bytesConstRef data, bytes& out)
{
    dev::eth::ContractABI abi;

    Address contractAddress;
    abi.abiOut(data, contractAddress);

    std::string tableName = precompiled::getContractTableName(contractAddress);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractStatusPrecompiled")
                           << LOG_DESC("call query status")
                           << LOG_KV("contract table name", tableName);

    int status = getContractStatus(context, tableName);
    out = abi.abiIn("", (u256)0, CONTRACT_STATUS_DESC[status]);
}

void ContractStatusPrecompiled::listManager(
    ExecutiveContext::Ptr context, bytesConstRef data, bytes& out)
{
    dev::eth::ContractABI abi;

    Address contractAddress;
    abi.abiOut(data, contractAddress);

    std::string tableName = precompiled::getContractTableName(contractAddress);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractStatusPrecompiled")
                           << LOG_DESC("call query authority")
                           << LOG_KV("contract table name", tableName);

    std::vector<Address> addrs;
    Table::Ptr table = openTable(context, tableName);
    if (table)
    {
        auto entries = table->select(storagestate::ACCOUNT_AUTHORITY, table->newCondition());
        if (entries->size() > 0)
        {
            for (size_t i = 0; i < entries->size(); i++)
            {
                auto authority = entries->get(i)->getField(storagestate::STORAGE_VALUE);
                addrs.push_back(dev::eth::toAddress(authority));
            }
        }
        else
        {
            PRECOMPILED_LOG(ERROR)
                << LOG_BADGE("ContractStatusPrecompiled") << LOG_DESC("no authority is recorded");
        }
    }

    out = abi.abiIn("", (u256)0, addrs);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractStatusPrecompiled")
                           << LOG_DESC("call query authority result")
                           << LOG_KV("out", dev::toHex(out));
}

bytes ContractStatusPrecompiled::call(
    ExecutiveContext::Ptr context, bytesConstRef param, Address const& origin, Address const&)
{
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ContractStatusPrecompiled")
                           << LOG_KV("call param", toHex(param));

    // parse function name
    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);
    bytes out;

    if (func == name2Selector[METHOD_FREEZE_STR])
    {
        freeze(context, data, origin, out);
    }
    else if (func == name2Selector[METHOD_UNFREEZE_STR])
    {
        unfreeze(context, data, origin, out);
    }
    else if (func == name2Selector[METHOD_GRANT_STR])
    {
        grantManager(context, data, origin, out);
    }
    else if (func == name2Selector[METHOD_QUERY_STR])
    {
        getStatus(context, data, out);
    }
    else if (func == name2Selector[METHOD_QUERY_AUTHORITY])
    {
        listManager(context, data, out);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ContractStatusPrecompiled")
                               << LOG_DESC("call undefined function") << LOG_KV("func", func);
    }

    return out;
}
