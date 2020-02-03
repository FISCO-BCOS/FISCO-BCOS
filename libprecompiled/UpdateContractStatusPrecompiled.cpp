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
/** @file UpdateContractStatusPrecompiled.cpp
 *  @author chaychen
 *  @date 20190106
 */
#include "UpdateContractStatusPrecompiled.h"

#include "libprecompiled/EntriesPrecompiled.h"
#include "libprecompiled/TableFactoryPrecompiled.h"
#include "libstoragestate/StorageState.h"
#include <libethcore/ABI.h>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::storage;
using namespace dev::precompiled;

// precompiled contract function
/*
contract Frozen {
    function kill(address addr) public returns(int);
    function freeze(address addr) public returns(int);
    function unfreeze(address addr) public returns(int);
    function queryStatus(address addr) public constant returns(uint,string);
}
*/

const char* const METHOD_KILL_STR = "kill(address)";
const char* const METHOD_FREEZE_STR = "freeze(address)";
const char* const METHOD_UNFREEZE_STR = "unfreeze(address)";
const char* const METHOD_QUERY_STR = "queryStatus(address)";

// contract state
// available,frozen,killed

// state-transition matrix
/*
            available frozen    killed
   freeze   frozen    ×         ×
   unfreeze ×         available ×
   kill     killed    killed    ×
*/

UpdateContractStatusPrecompiled::UpdateContractStatusPrecompiled()
{
    name2Selector[METHOD_KILL_STR] = getFuncSelector(METHOD_KILL_STR);
    name2Selector[METHOD_FREEZE_STR] = getFuncSelector(METHOD_FREEZE_STR);
    name2Selector[METHOD_UNFREEZE_STR] = getFuncSelector(METHOD_UNFREEZE_STR);
    name2Selector[METHOD_QUERY_STR] = getFuncSelector(METHOD_QUERY_STR);
}

bool UpdateContractStatusPrecompiled::checkAddress(Address const& contractAddress)
{
    bool isValidAddress = true;
    try
    {
        Address address(contractAddress);
        (void)address;
    }
    catch (...)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("UpdateContractStatusPrecompiled")
                               << LOG_DESC("address invalid") << LOG_KV("address", contractAddress);
        isValidAddress = false;
    }
    return isValidAddress;
}

ContractStatus UpdateContractStatusPrecompiled::getContractStatus(
    ExecutiveContext::Ptr context, std::string const& tableName)
{
    ContractStatus status = ContractStatus::Invalid;

    Table::Ptr table = openTable(context, tableName);
    if (table)
    {
        auto entries = table->select(storagestate::ACCOUNT_ALIVE, table->newCondition());
        if (entries->size() > 0 &&
            STATUS_FALSE == entries->get(0)->getField(storagestate::STORAGE_VALUE))
        {
            status = ContractStatus::Killed;
        }
        else
        {  // query only in alive
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
    }
    else
    {  // valid address but nonexistent
        status = ContractStatus::Nonexistent;
    }

    if (ContractStatus::Invalid == status)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("UpdateContractStatusPrecompiled")
                               << LOG_DESC("getContractStatus error")
                               << LOG_KV("table name", tableName);
    }
    return status;
}

void UpdateContractStatusPrecompiled::kill(
    ExecutiveContext::Ptr context, bytesConstRef data, Address const& origin, bytes& out)
{
    dev::eth::ContractABI abi;
    Address contractAddress;
    abi.abiOut(data, contractAddress);
    int result = 0;
    // check address valid
    if (!checkAddress(contractAddress))
    {
        result = CODE_ADDRESS_INVALID;
    }
    else
    {
        std::string tableName = precompiled::getContractTableName(contractAddress);
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("UpdateContractStatusPrecompiled")
                               << LOG_KV("kill contract", tableName);

        ContractStatus status = getContractStatus(context, tableName);
        if (ContractStatus::Killed == status)
        {
            result = CODE_INVALID_CONTRACT_KILLED;
        }
        else if (ContractStatus::Nonexistent == status)
        {
            result = CODE_TABLE_AND_ADDRESS_NOT_EXIST;
        }
        else
        {
            Table::Ptr table = openTable(context, tableName);
            if (table)
            {
                auto entry = table->newEntry();
                entry->setField(storagestate::STORAGE_VALUE, "");
                table->update(storagestate::ACCOUNT_CODE, entry, table->newCondition(),
                    std::make_shared<AccessOptions>(origin));
                entry = table->newEntry();
                entry->setField(storagestate::STORAGE_VALUE, toHex(EmptySHA3));
                table->update(storagestate::ACCOUNT_CODE_HASH, entry, table->newCondition(),
                    std::make_shared<AccessOptions>(origin));
                entry = table->newEntry();
                entry->setField(storagestate::STORAGE_VALUE, STATUS_FALSE);
                result = table->update(storagestate::ACCOUNT_ALIVE, entry, table->newCondition(),
                    std::make_shared<AccessOptions>(origin));

                if (result == storage::CODE_NO_AUTHORIZED)
                {
                    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("UpdateContractStatusPrecompiled")
                                           << LOG_DESC("permission denied");
                }
            }
        }
    }

    getErrorCodeOut(out, result);
}

int UpdateContractStatusPrecompiled::updateFrozenStatus(ExecutiveContext::Ptr context,
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
                std::make_shared<AccessOptions>(origin));
        }
        else
        {
            result = table->insert(
                storagestate::ACCOUNT_FROZEN, entry, std::make_shared<AccessOptions>(origin));
        }

        if (result == storage::CODE_NO_AUTHORIZED)
        {
            PRECOMPILED_LOG(DEBUG)
                << LOG_BADGE("UpdateContractStatusPrecompiled") << LOG_DESC("permission denied");
        }
    }

    return result;
}

void UpdateContractStatusPrecompiled::freeze(
    ExecutiveContext::Ptr context, bytesConstRef data, Address const& origin, bytes& out)
{
    dev::eth::ContractABI abi;
    Address contractAddress;
    abi.abiOut(data, contractAddress);
    int result = 0;
    // check address valid and status(available)
    if (!checkAddress(contractAddress))
    {
        result = CODE_ADDRESS_INVALID;
    }
    else
    {
        std::string tableName = precompiled::getContractTableName(contractAddress);
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("UpdateContractStatusPrecompiled")
                               << LOG_KV("freeze contract", tableName);

        ContractStatus status = getContractStatus(context, tableName);
        if (ContractStatus::Killed == status)
        {
            result = CODE_INVALID_CONTRACT_KILLED;
        }
        else if (ContractStatus::Frozen == status)
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
    getErrorCodeOut(out, result);
}

void UpdateContractStatusPrecompiled::unfreeze(
    ExecutiveContext::Ptr context, bytesConstRef data, Address const& origin, bytes& out)
{
    dev::eth::ContractABI abi;
    Address contractAddress;
    abi.abiOut(data, contractAddress);
    int result = 0;

    if (!checkAddress(contractAddress))
    {
        result = CODE_ADDRESS_INVALID;
    }
    else
    {
        std::string tableName = precompiled::getContractTableName(contractAddress);
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("UpdateContractStatusPrecompiled")
                               << LOG_KV("unfreeze contract", tableName);

        ContractStatus status = getContractStatus(context, tableName);
        if (ContractStatus::Killed == status)
        {
            result = CODE_INVALID_CONTRACT_KILLED;
        }
        else if (ContractStatus::Available == status)
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
    getErrorCodeOut(out, result);
}

void UpdateContractStatusPrecompiled::query(
    ExecutiveContext::Ptr context, bytesConstRef data, bytes& out)
{
    dev::eth::ContractABI abi;

    Address contractAddress;
    abi.abiOut(data, contractAddress);

    std::string tableName = precompiled::getContractTableName(contractAddress);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("UpdateContractStatusPrecompiled") << LOG_DESC("call query")
                           << LOG_KV("contract table name", tableName);

    int status = getContractStatus(context, tableName);
    out = abi.abiIn("", (u256)status, CONTRACT_STATUS_DESC[status]);
}

bytes UpdateContractStatusPrecompiled::call(
    ExecutiveContext::Ptr context, bytesConstRef param, Address const& origin)
{
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("UpdateContractStatusPrecompiled")
                           << LOG_KV("call param", toHex(param));

    // parse function name
    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);
    bytes out;

    if (func == name2Selector[METHOD_KILL_STR])
    {
        kill(context, data, origin, out);
    }
    else if (func == name2Selector[METHOD_FREEZE_STR])
    {
        freeze(context, data, origin, out);
    }
    else if (func == name2Selector[METHOD_UNFREEZE_STR])
    {
        unfreeze(context, data, origin, out);
    }
    else if (func == name2Selector[METHOD_QUERY_STR])
    {
        query(context, data, out);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("UpdateContractStatusPrecompiled")
                               << LOG_DESC("call undefined function") << LOG_KV("func", func);
    }

    return out;
}
