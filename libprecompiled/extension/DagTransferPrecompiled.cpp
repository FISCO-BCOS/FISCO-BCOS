
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
/** @file DagTransferPrecompiled.cpp
 *  @author octopuswang
 *  @date 20190111
 */
#include "DagTransferPrecompiled.h"
#include <libethcore/ABI.h>
#include <libprecompiled/TableFactoryPrecompiled.h>

using namespace std;
using namespace bcos;
using namespace bcos::blockverifier;
using namespace bcos::storage;
using namespace bcos::precompiled;

// interface of DagTransferPrecompiled
/*
contract DagTransfer{
    function userAdd(string user, uint256 balance) public returns();
    function userSave(string user, uint256 balance) public returns(uint256);
    function userDraw(string user, uint256 balance) public returns(uint256);
    function userBalance(string user) public constant returns(uint256,uint256);
    function userTransfer(string user_a, string user_b, uint256 amount) public returns(uint256);
}
*/
const std::string DAG_TRANSFER = "dag_transfer";
const char* const DAG_TRANSFER_METHOD_ADD_STR_UINT = "userAdd(string,uint256)";
const char* const DAG_TRANSFER_METHOD_SAV_STR_UINT = "userSave(string,uint256)";
const char* const DAG_TRANSFER_METHOD_DRAW_STR_UINT = "userDraw(string,uint256)";
const char* const DAG_TRANSFER_METHOD_TRS_STR2_UINT = "userTransfer(string,string,uint256)";
const char* const DAG_TRANSFER_METHOD_BAL_STR = "userBalance(string)";

// fields of table '_dag_transfer_'
const std::string DAG_TRANSFER_FIELD_NAME = "user_name";
const std::string DAG_TRANSFER_FIELD_BALANCE = "user_balance";

DagTransferPrecompiled::DagTransferPrecompiled()
{
    name2Selector[DAG_TRANSFER_METHOD_ADD_STR_UINT] =
        getFuncSelector(DAG_TRANSFER_METHOD_ADD_STR_UINT);
    name2Selector[DAG_TRANSFER_METHOD_SAV_STR_UINT] =
        getFuncSelector(DAG_TRANSFER_METHOD_SAV_STR_UINT);
    name2Selector[DAG_TRANSFER_METHOD_DRAW_STR_UINT] =
        getFuncSelector(DAG_TRANSFER_METHOD_DRAW_STR_UINT);
    name2Selector[DAG_TRANSFER_METHOD_TRS_STR2_UINT] =
        getFuncSelector(DAG_TRANSFER_METHOD_TRS_STR2_UINT);
    name2Selector[DAG_TRANSFER_METHOD_BAL_STR] = getFuncSelector(DAG_TRANSFER_METHOD_BAL_STR);
}

bool DagTransferPrecompiled::invalidUserName(const std::string& strUserName)
{
    // user name not empty means invalid or other limits exist?
    return strUserName.empty();
}

std::vector<std::string> DagTransferPrecompiled::getParallelTag(bytesConstRef param)
{
    // parse function name
    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    std::vector<std::string> results;
    bcos::eth::ContractABI abi;
    // user_name user_balance 2 fields in table, the key of table is user_name field
    if (func == name2Selector[DAG_TRANSFER_METHOD_ADD_STR_UINT])
    {  // userAdd(string,uint256)
        std::string user;
        bcos::u256 amount;
        abi.abiOut(data, user, amount);
        // if params is invalid , parallel process can be done
        if (!invalidUserName(user))
        {
            results.push_back(user);
        }
    }
    else if (func == name2Selector[DAG_TRANSFER_METHOD_SAV_STR_UINT])
    {  // userSave(string,uint256)
        std::string user;
        bcos::u256 amount;

        abi.abiOut(data, user, amount);
        // if params is invalid , parallel process can be done
        if (!invalidUserName(user))
        {
            results.push_back(user);
        }
    }
    else if (func == name2Selector[DAG_TRANSFER_METHOD_DRAW_STR_UINT])
    {  // userDraw(string,uint256)
        std::string user;
        bcos::u256 amount;

        abi.abiOut(data, user, amount);
        // if params is invalid , parallel process can be done
        if (!invalidUserName(user))
        {
            results.push_back(user);
        }
    }
    else if (func == name2Selector[DAG_TRANSFER_METHOD_TRS_STR2_UINT])
    {
        // userTransfer(string,string,uint256)
        std::string fromUser, toUser;
        bcos::u256 amount;

        abi.abiOut(data, fromUser, toUser, amount);
        // if params is invalid , parallel process can be done
        if (!invalidUserName(fromUser) && !invalidUserName(toUser))
        {
            results.push_back(fromUser);
            results.push_back(toUser);
        }
    }
    else if (func == name2Selector[DAG_TRANSFER_METHOD_BAL_STR])
    {
        // query interface has no parallel processing conflict.
        // do nothing
    }

    return results;
}

std::string DagTransferPrecompiled::toString()
{
    return "DagTransfer";
}

Table::Ptr DagTransferPrecompiled::openTable(
    bcos::blockverifier::ExecutiveContext::Ptr context, Address const& origin)
{
    string dagTableName;
    if (g_BCOSConfig.version() < V2_2_0)
    {
        dagTableName = "_dag_transfer_";
    }
    else
    {
        dagTableName = precompiled::getTableName(DAG_TRANSFER);
    }
    auto table = bcos::precompiled::openTable(context, dagTableName);
    if (!table)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE(
                                      "DagTransferPrecompiled openTable: ready to create table")
                               << LOG_KV("tableName", dagTableName);
        //__dag_transfer__ is not exist, then create it first.
        table = createTable(
            context, dagTableName, DAG_TRANSFER_FIELD_NAME, DAG_TRANSFER_FIELD_BALANCE, origin);
        // table already exists
        if (!table)
        {
            PRECOMPILED_LOG(DEBUG) << LOG_BADGE("DagTransferPrecompiled: table already exist")
                                   << LOG_KV("tableName", dagTableName);
            // try to openTable and get the table again
            table = bcos::precompiled::openTable(context, dagTableName);
        }
    }
    return table;
}

PrecompiledExecResult::Ptr DagTransferPrecompiled::call(
    bcos::blockverifier::ExecutiveContext::Ptr context, bytesConstRef param, Address const& origin,
    Address const&)
{
    // PRECOMPILED_LOG(TRACE) << LOG_BADGE("DagTransferPrecompiled") << LOG_DESC("call")
    //                       << LOG_KV("param", toHex(param));

    // parse function name
    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);
    auto callResult = m_precompiledExecResultFactory->createPrecompiledResult();
    // user_name user_balance 2 fields in table, the key of table is user_name field
    if (func == name2Selector[DAG_TRANSFER_METHOD_ADD_STR_UINT])
    {  // userAdd(string,uint256)
        userAddCall(context, data, origin, callResult->mutableExecResult());
    }
    else if (func == name2Selector[DAG_TRANSFER_METHOD_SAV_STR_UINT])
    {  // userSave(string,uint256)
        userSaveCall(context, data, origin, callResult->mutableExecResult());
    }
    else if (func == name2Selector[DAG_TRANSFER_METHOD_DRAW_STR_UINT])
    {  // userDraw(string,uint256)
        userDrawCall(context, data, origin, callResult->mutableExecResult());
    }
    else if (func == name2Selector[DAG_TRANSFER_METHOD_TRS_STR2_UINT])
    {  // userTransfer(string,string,uint256)
        userTransferCall(context, data, origin, callResult->mutableExecResult());
    }
    else if (func == name2Selector[DAG_TRANSFER_METHOD_BAL_STR])
    {  // userBalance(string user)
        userBalanceCall(context, data, origin, callResult->mutableExecResult());
    }
    else
    {
        // PRECOMPILED_LOG(ERROR) << LOG_BADGE("DagTransferPrecompiled") << LOG_DESC("error func")
        //                       << LOG_KV("func", func);
    }

    // PRECOMPILED_LOG(TRACE) << LOG_BADGE("DagTransferPrecompiled") << LOG_DESC("call")
    //                       << LOG_DESC("end");

    return callResult;
}

void DagTransferPrecompiled::userAddCall(bcos::blockverifier::ExecutiveContext::Ptr context,
    bytesConstRef data, Address const& origin, bytes& out)
{  // userAdd(string,uint256)
    std::string user;
    bcos::u256 amount;
    bcos::eth::ContractABI abi;
    abi.abiOut(data, user, amount);

    int ret;
    std::string strErrorMsg;
    do
    {
        if (invalidUserName(user))
        {
            strErrorMsg = "invalid user name";
            ret = CODE_INVALID_USER_NAME;
            break;
        }

        Table::Ptr table = openTable(context, origin);
        if (!table)
        {
            strErrorMsg = "openTable failed.";
            ret = CODE_INVALID_OPENTABLE_FAILED;
            break;
        }

        auto entries = table->select(user, table->newCondition());
        if (entries.get() && (0u != entries->size()))
        {
            strErrorMsg = "user already exist";
            ret = CODE_INVALID_USER_ALREADY_EXIST;
            break;
        }

        // user not exist, insert user into it.
        auto entry = table->newEntry();
        entry->setField(DAG_TRANSFER_FIELD_NAME, user);
        entry->setField(DAG_TRANSFER_FIELD_BALANCE, amount.str());
        entry->setForce(true);

        auto count = table->insert(user, entry, std::make_shared<AccessOptions>(origin));
        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            strErrorMsg = "permission denied";
            ret = CODE_NO_AUTHORIZED;
            break;
        }

        // end success
        ret = 0;
    } while (0);

    out = abi.abiIn("", u256(ret));
}

void DagTransferPrecompiled::userSaveCall(bcos::blockverifier::ExecutiveContext::Ptr context,
    bytesConstRef data, Address const& origin, bytes& out)
{  // userSave(string,uint256)
    std::string user;
    bcos::u256 amount;
    bcos::eth::ContractABI abi;
    abi.abiOut(data, user, amount);

    int ret;
    bcos::u256 balance;
    std::string strErrorMsg;

    do
    {
        if (invalidUserName(user))
        {
            strErrorMsg = "invalid user name";
            ret = CODE_INVALID_USER_NAME;
            break;
        }

        // check amount valid
        if (0 == amount)
        {
            strErrorMsg = "invalid save amount";
            ret = CODE_INVALID_AMOUNT;
            break;
        }

        Table::Ptr table = openTable(context, origin);
        if (!table)
        {
            strErrorMsg = "openTable failed.";
            ret = CODE_INVALID_OPENTABLE_FAILED;
            break;
        }

        auto entries = table->select(user, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            // If user is not exist, insert it. With this strategy, we can also add user by save
            // opeation.

            auto entry = table->newEntry();
            entry->setField(DAG_TRANSFER_FIELD_NAME, user);
            entry->setField(DAG_TRANSFER_FIELD_BALANCE, amount.str());

            auto count = table->insert(user, entry, std::make_shared<AccessOptions>(origin));
            if (count == CODE_NO_AUTHORIZED)
            {  // permission denied
                strErrorMsg = "permission denied";
                ret = CODE_NO_AUTHORIZED;
                break;
            }
        }
        else
        {
            auto entry = entries->get(0);  // only one record for every user
            balance = bcos::u256(entry->getField(DAG_TRANSFER_FIELD_BALANCE));

            // if overflow
            auto new_balance = balance + amount;
            if (new_balance < balance)
            {
                strErrorMsg = "save overflow";
                ret = CODE_INVALID_BALANCE_OVERFLOW;
                break;
            }

            auto updateEntry = table->newEntry();
            updateEntry->setField(DAG_TRANSFER_FIELD_NAME, user);
            updateEntry->setField(DAG_TRANSFER_FIELD_BALANCE, new_balance.str());

            auto count = table->update(
                user, updateEntry, table->newCondition(), std::make_shared<AccessOptions>(origin));
            if (count == CODE_NO_AUTHORIZED)
            {  // permission denied
                strErrorMsg = "permission denied";
                ret = CODE_NO_AUTHORIZED;
                break;
            }
        }

        ret = 0;
    } while (0);

    out = abi.abiIn("", u256(ret));
}

void DagTransferPrecompiled::userDrawCall(bcos::blockverifier::ExecutiveContext::Ptr context,
    bytesConstRef data, Address const& origin, bytes& out)
{
    std::string user;
    bcos::u256 amount;
    bcos::eth::ContractABI abi;
    abi.abiOut(data, user, amount);

    bcos::u256 balance;
    int ret;
    std::string strErrorMsg;

    do
    {
        if (invalidUserName(user))
        {
            strErrorMsg = "invalid user name";
            ret = CODE_INVALID_USER_NAME;
            break;
        }

        if (amount == 0)
        {
            strErrorMsg = "draw invalid amount";
            ret = CODE_INVALID_AMOUNT;
            break;
        }

        Table::Ptr table = openTable(context, origin);
        if (!table)
        {
            strErrorMsg = "openTable failed.";
            ret = CODE_INVALID_OPENTABLE_FAILED;
            break;
        }

        auto entries = table->select(user, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            strErrorMsg = "user not exist";
            ret = CODE_INVALID_USER_NOT_EXIST;
            break;
        }

        // only one record for every user
        balance = bcos::u256(entries->get(0)->getField(DAG_TRANSFER_FIELD_BALANCE));
        if (balance < amount)
        {
            strErrorMsg = "insufficient balance";
            ret = CODE_INVALID_INSUFFICIENT_BALANCE;
            break;
        }

        auto new_balance = balance - amount;
        auto entry = table->newEntry();
        entry->setField(DAG_TRANSFER_FIELD_NAME, user);
        entry->setField(DAG_TRANSFER_FIELD_BALANCE, new_balance.str());

        auto count = table->update(
            user, entry, table->newCondition(), std::make_shared<AccessOptions>(origin));
        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            strErrorMsg = "permission denied";
            ret = CODE_NO_AUTHORIZED;
            break;
        }

        ret = 0;
    } while (0);

    out = abi.abiIn("", u256(ret));
}

void DagTransferPrecompiled::userBalanceCall(bcos::blockverifier::ExecutiveContext::Ptr context,
    bytesConstRef data, Address const& origin, bytes& out)
{
    std::string user;
    bcos::eth::ContractABI abi;
    abi.abiOut(data, user);

    bcos::u256 balance;
    int ret;
    std::string strErrorMsg;

    do
    {
        if (invalidUserName(user))
        {
            strErrorMsg = " invalid user name";
            ret = CODE_INVALID_USER_NAME;
            break;
        }

        Table::Ptr table = openTable(context, origin);
        if (!table)
        {
            strErrorMsg = "openTable failed.";
            ret = CODE_INVALID_OPENTABLE_FAILED;
            break;
        }

        auto entries = table->select(user, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            strErrorMsg = " user not exist";
            ret = CODE_INVALID_USER_NOT_EXIST;
            break;
        }

        // only one record for every user
        balance = bcos::u256(entries->get(0)->getField(DAG_TRANSFER_FIELD_BALANCE));
        ret = 0;
    } while (0);

    out = abi.abiIn("", u256(ret), balance);
}

void DagTransferPrecompiled::userTransferCall(
    ExecutiveContext::Ptr context, bytesConstRef data, Address const& origin, bytes& out)
{
    std::string fromUser, toUser;
    bcos::u256 amount;
    bcos::eth::ContractABI abi;
    abi.abiOut(data, fromUser, toUser, amount);

    bcos::u256 fromUserBalance, newFromUserBalance;
    bcos::u256 toUserBalance, newToUserBalance;

    std::string strErrorMsg;
    int ret;

    do
    {
        // parameters check
        if (invalidUserName(fromUser) || invalidUserName(toUser))
        {
            strErrorMsg = "invalid user name";
            ret = CODE_INVALID_USER_NAME;
            break;
        }

        if (amount == 0)
        {
            strErrorMsg = "invalid amount";
            ret = CODE_INVALID_AMOUNT;
            break;
        }

        // transfer self, do nothing
        if (fromUser == toUser)
        {
            ret = 0;
            break;
        }

        Table::Ptr table = openTable(context, origin);
        if (!table)
        {
            strErrorMsg = "openTable failed.";
            ret = CODE_INVALID_OPENTABLE_FAILED;
            break;
        }

        auto entries = table->select(fromUser, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            strErrorMsg = "from user not exist";
            ret = CODE_INVALID_USER_NOT_EXIST;
            break;
        }

        fromUserBalance = bcos::u256(entries->get(0)->getField(DAG_TRANSFER_FIELD_BALANCE));
        if (fromUserBalance < amount)
        {
            strErrorMsg = "from user insufficient balance";
            ret = CODE_INVALID_INSUFFICIENT_BALANCE;
            break;
        }

        entries = table->select(toUser, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            // If to user not exist, add it first.
            auto entry = table->newEntry();
            entry->setField(DAG_TRANSFER_FIELD_NAME, toUser);
            entry->setField(DAG_TRANSFER_FIELD_BALANCE, bcos::u256(0).str());

            auto count = table->insert(toUser, entry, std::make_shared<AccessOptions>(origin));
            if (count == CODE_NO_AUTHORIZED)
            {  // permission denied
                strErrorMsg = "permission denied";
                ret = CODE_NO_AUTHORIZED;
                break;
            }
            toUserBalance = 0;
        }
        else
        {
            toUserBalance = bcos::u256(entries->get(0)->getField(DAG_TRANSFER_FIELD_BALANCE));
        }

        // overflow check
        if (toUserBalance + amount < toUserBalance)
        {
            strErrorMsg = "to user balance overflow.";
            ret = CODE_INVALID_BALANCE_OVERFLOW;
            break;
        }

        newFromUserBalance = fromUserBalance - amount;
        newToUserBalance = toUserBalance + amount;

        // update fromUser balance info.
        auto entry = table->newEntry();
        entry->setField(DAG_TRANSFER_FIELD_NAME, fromUser);
        entry->setField(DAG_TRANSFER_FIELD_BALANCE, newFromUserBalance.str());
        auto count = table->update(
            fromUser, entry, table->newCondition(), std::make_shared<AccessOptions>(origin));
        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            strErrorMsg = "permission denied";
            ret = CODE_NO_AUTHORIZED;
            break;
        }

        // update toUser balance info.
        entry = table->newEntry();
        entry->setField(DAG_TRANSFER_FIELD_NAME, toUser);
        entry->setField(DAG_TRANSFER_FIELD_BALANCE, newToUserBalance.str());
        count = table->update(
            toUser, entry, table->newCondition(), std::make_shared<AccessOptions>(origin));

        // end with success
        ret = 0;
    } while (0);

    out = abi.abiIn("", u256(ret));
}
