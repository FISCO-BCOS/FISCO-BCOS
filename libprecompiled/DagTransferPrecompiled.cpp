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
#include <json_spirit/JsonSpiritHeaders.h>
#include <libdevcore/easylog.h>
#include <libethcore/ABI.h>
#include <libstorage/EntriesPrecompiled.h>
#include <libstorage/TableFactoryPrecompiled.h>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::storage;
using namespace dev::precompiled;

/*
contract DagTransfer{
    function userAdd(string user, uint256 balance) public returns(bool);
    function userSave(string user, uint256 balance) public returns(bool);
    function userDraw(string user, uint256 balance) public returns(bool);
    function userBalance(string user) public constant returns(bool,uint256);
    function userTransfer(string user_a, string user_b, uint256 amount) public returns(bool);
}
*/

const char* const DAG_TRANSFER_METHOD_ADD_STR_UINT = "userAdd(string,uint256)";
const char* const DAG_TRANSFER_METHOD_SAV_STR_UINT = "userSave(string,uint256)";
const char* const DAG_TRANSFER_METHOD_DRAW_STR_UINT = "userDraw(string,uint256)";
const char* const DAG_TRANSFER_METHOD_TRS_STR2_UINT = "userTransfer(string,string,uint256)";
const char* const DAG_TRANSFER_METHOD_BAL_STR = "userBalance(string)";

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

std::vector<std::string> DagTransferPrecompiled::getDagTag(bytesConstRef param)
{
    // parse function name
    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    std::vector<std::string> results;
    dev::eth::ContractABI abi;
    // user_name user_balance 2 fields in table, the key of table is user_name field
    if (func == name2Selector[DAG_TRANSFER_METHOD_ADD_STR_UINT])
    {  // userAdd(string,uint256)
        std::string user;
        dev::u256 amount;
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
        dev::u256 amount;

        abi.abiOut(data, user, amount);
        // if params is invalid , parallel process can be done
        if (!invalidUserName(user) && (amount > 0))
        {
            results.push_back(user);
        }
    }
    else if (func == name2Selector[DAG_TRANSFER_METHOD_DRAW_STR_UINT])
    {  // userDraw(string,uint256)
        std::string user;
        dev::u256 amount;

        abi.abiOut(data, user, amount);
        // if params is invalid , parallel process can be done
        if (!invalidUserName(user) && (amount > 0))
        {
            results.push_back(user);
        }
    }
    else if (func == name2Selector[DAG_TRANSFER_METHOD_TRS_STR2_UINT])
    {  // userTransfer(string,string,uint256)
        std::string fromUser, toUser;
        dev::u256 amount;

        abi.abiOut(data, fromUser, toUser, amount);
        // if params is invalid , parallel process can be done
        if (!invalidUserName(fromUser) && !invalidUserName(toUser) && (amount > 0))
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

std::string DagTransferPrecompiled::toString(dev::blockverifier::ExecutiveContext::Ptr)
{
    return "DagTransfer";
}

Table::Ptr DagTransferPrecompiled::openTable(
    ExecutiveContext::Ptr context, const std::string& tableName)
{
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("DagTransferPrecompiled") << LOG_DESC("open table")
                           << LOG_KV("tableName", tableName);

    TableFactoryPrecompiled::Ptr tableFactoryPrecompiled =
        std::dynamic_pointer_cast<TableFactoryPrecompiled>(
            context->getPrecompiled(Address(0x1001)));
    return tableFactoryPrecompiled->getmemoryTableFactory()->openTable(tableName);
}

bytes DagTransferPrecompiled::call(
    dev::blockverifier::ExecutiveContext::Ptr context, bytesConstRef param, Address const& origin)
{
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("DagTransferPrecompiled") << LOG_DESC("call")
                           << LOG_KV("param", toHex(param));

    // parse function name
    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    bytes out;
    // user_name user_balance 2 fields in table, the key of table is user_name field
    if (func == name2Selector[DAG_TRANSFER_METHOD_ADD_STR_UINT])
    {  // userAdd(string,uint256)
        userAddCall(context, data, out)
    }
    else if (func == name2Selector[DAG_TRANSFER_METHOD_SAV_STR_UINT])
    {  // userSave(string,uint256)
        userSaveCall(context, data, out);
    }
    else if (func == name2Selector[DAG_TRANSFER_METHOD_DRAW_STR_UINT])
    {  // userDraw(string,uint256)
        userDrawCall(context, data, out);
    }
    else if (func == name2Selector[DAG_TRANSFER_METHOD_TRS_STR2_UINT])
    {  // userTransfer(string,string,uint256)
        userTransferCall(context, data, out);
    }
    else if (func == name2Selector[DAG_TRANSFER_METHOD_BAL_STR])
    {  // userBalance(string user)
        userBalanceCall(context, data, out);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("DagTransferPrecompiled") << LOG_DESC("error func")
                               << LOG_KV("func", func);
    }

    PRECOMPILED_LOG(TRACE) << LOG_BADGE("DagTransferPrecompiled") << LOG_DESC("call")
                           << LOG_DESC("end");

    return out;
}

void DagTransferPrecompiled::userAddCall(dev::blockverifier::ExecutiveContext::Ptr context,
    bytesConstRef data, Address const& origin, bytes& out)
{  // userAdd(string,uint256)

    std::string user;
    dev::u256 amount;
    dev::eth::ContractABI abi;
    abi.abiOut(data, user, amount);

    bool result = false;
    std::string strErrorMsg;
    do
    {
        if (invalidUserName(user))
        {
            strErrorMsg = "invalid user name";
            break;
        }

        Table::Ptr table = openTable(context, DAG_TRANSFER);
        auto entries = table->select(user, table->newCondition());
        if (entries.get() && (0u != entries->size()))
        {
            strErrorMsg = "user already exist";
            break;
        }

        // user not exist, insert user into it.
        auto entry = table->newEntry();
        entry->setField(DAG_TRANSFER_FIELD_NAME, user);
        entry->setField(DAG_TRANSFER_FIELD_BALANCE, amount.str());

        auto count = table->insert(user, entry, getOptions(origin));
        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            strErrorMsg = "non-authorized";
            break;
        }

        // end success
        result = true;

    } while (0);

    if (result)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("DagTransferPrecompiled") << LOG_DESC("userAddCall")
                               << LOG_KV("user", user) << LOG_KV("amount", amount);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("DagTransferPrecompiled") << LOG_DESC("userAddCall")
                               << LOG_KV("user", user) << LOG_KV("amount", amount)
                               << LOG_DESC(strErrorMsg);
    }


    out = abi.abiIn("", result);
}

void DagTransferPrecompiled::userSaveCall(dev::blockverifier::ExecutiveContext::Ptr context,
    bytesConstRef data, Address const& origin, bytes& out)
{  // userSave(string,uint256)
    std::string user;
    dev::u256 amount;
    dev::eth::ContractABI abi;
    abi.abiOut(data, user, amount);

    bool result = false;
    dev::u256 balance;

    do
    {
        if (invalidUserName(user))
        {
            strErrorMsg = "invalid user name";
            break;
        }

        // check amount valid
        if (0 == amount)
        {
            strErrorMsg = "invalid save amount";
            break;
        }

        Table::Ptr table = openTable(context, DAG_TRANSFER);
        auto entries = table->select(user, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            // If user is not exist, insert it. With this strategy, we can also add user by save
            // opeation.

            auto entry = table->newEntry();
            entry->setField(DAG_TRANSFER_FIELD_NAME, user);
            entry->setField(DAG_TRANSFER_FIELD_BALANCE, amount.str());

            auto count = table->insert(user, entry, getOptions(origin));
            if (count == CODE_NO_AUTHORIZED)
            {  // permission denied
                strErrorMsg = "non-authorized";
                break;
            }
        }
        else
        {
            entry = entries->get(0);  // only one record for every user
            balance = dev::u256(entry->getField(DAG_TRANSFER_FIELD_BALANCE)));

            // if overflow
            auto new_balance = balance + amount;
            if (new_balance < balance)
            {
                strErrorMsg = "save overflow";
                break;
            }

            auto entry = table->newEntry();
            entry->setField(DAG_TRANSFER_FIELD_NAME, user);
            entry->setField(DAG_TRANSFER_FIELD_BALANCE, new_balance.str());

            auto count = table->update(user, entry, getOptions(origin));
            if (count == CODE_NO_AUTHORIZED)
            {  // permission denied
                strErrorMsg = "non-authorized";
                break;
            }
        }

        result = true;

    } while (0);

    if (result)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("DagTransferPrecompiled") << LOG_DESC("userSaveCall")
                               << LOG_KV("user", user) << LOG_KV("amount", amount)
                               << LOG_KV("balance", balance);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("DagTransferPrecompiled") << LOG_DESC("userSaveCall")
                               << LOG_KV("user", user) << LOG_KV("amount", amount)
                               << LOG_KV("balance", balance) << LOG_DESC(strErrorMsg);
    }

    out = abi.abiIn("", result);
}

void DagTransferPrecompiled::userDrawCall(dev::blockverifier::ExecutiveContext::Ptr context,
    bytesConstRef data, Address const& origin, bytes& out)
{
    std::string user;
    dev::u256 amount;
    dev::eth::ContractABI abi;
    abi.abiOut(data, user);

    dev::u256 balance;
    bool result = false;

    do
    {
        if (invalidUserName(user))
        {
            strErrorMsg = "invalid user name";
            break;
        }

        if (amount == 0)
        {
            strErrorMsg = "draw invalid amount";
            break;
        }

        Table::Ptr table = openTable(context, DAG_TRANSFER);
        auto entries = table->select(user, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            strErrorMsg = "user not exist";
            break;
        }

        // only one record for every user
        balance = dev::u256(entries->get(0)->getField(DAG_TRANSFER_FIELD_BALANCE)));
        if (balance < amount)
        {
            strErrorMsg = "insufficient balance";
            break;
        }

        auto new_balance = balance - amount;
        auto entry = table->newEntry();
        entry->setField(DAG_TRANSFER_FIELD_NAME, user);
        entry->setField(DAG_TRANSFER_FIELD_BALANCE, new_balance.str());

        auto count = table->update(user, entry, getOptions(origin));
        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            strErrorMsg = "non-authorized";
            break;
        }

        result = true;

    } while (0);

    if (result)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("DagTransferPrecompiled") << LOG_DESC("userDrawCall")
                               << LOG_KV("user", user) << LOG_KV("amount", amount)
                               << LOG_KV("balance", balance);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("DagTransferPrecompiled") << LOG_DESC("userDrawCall")
                               << LOG_KV("user", user) << LOG_KV("amount", amount)
                               << LOG_KV("balance", balance) << LOG_DESC(strErrorMsg);
    }

    out = abi.abiIn("", result);
}

void DagTransferPrecompiled::userBalanceCall(dev::blockverifier::ExecutiveContext::Ptr context,
    bytesConstRef data, Address const& origin, bytes& out)
{
    std::string user;
    dev::eth::ContractABI abi;
    abi.abiOut(data, user);

    dev::u256 balance;
    bool result = false;

    do
    {
        if (invalidUserName(user))
        {
            strErrorMsg = "invalid user name.";
            break;
        }

        Table::Ptr table = openTable(context, DAG_TRANSFER);
        auto entries = table->select(user, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            strErrorMsg = "user not exist";
            break;
        }

        // only one record for every user
        balance = dev::u256(entries->get(0)->getField(DAG_TRANSFER_FIELD_BALANCE)));
        result = true;

    } while (0);

    if (result)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("DagTransferPrecompiled") << LOG_DESC("userBalanceCall")
                               << LOG_KV("user", user) << LOG_KV("balance", balance);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("DagTransferPrecompiled") << LOG_DESC("userBalanceCall")
                               << LOG_KV("user", user) << LOG_KV("balance", balance)
                               << LOG_DESC(strErrorMsg);
    }

    out = abi.abiIn("", result, balance);
}

void DagTransferPrecompiled::userTransferCall(dev::blockverifier::ExecutiveContext::Ptr context,
    bytesConstRef data, Address const& origin, bytes& out)
{
    std::string fromUser, toUser;
    dev::u256 amount;
    dev::eth::ContractABI abi;
    abi.abiOut(data, fromUser, toUser, amount);

    dev::u256 fromUserBalance, newFromUserBalance;
    dev::u256 toUserBalance, newToUserBalance;

    std::string strErrorMsg;
    bool result = false;
    do
    {
        // parameters check
        if (invalidUserName(fromUser) || invalidUserName(toUser) || (amount == 0))
        {
            strErrorMsg = "invalid parameters";
            break;
        }


        Table::Ptr table = openTable(context, DAG_TRANSFER);
        auto entries = table->select(fromUser, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            strErrorMsg = "from user not exist";
            break;
        }

        fromUserBalance = dev::u256(entries->get(0)->getField(DAG_TRANSFER_FIELD_BALANCE)));
        if (fromUserBalance < amount)
        {
            strErrorMsg = "from user insufficient balance";
            break;
        }

        entries = table->select(toUser, table->newCondition());
        if (!entries.get() || (0u == entries->size()))
        {
            // If to user not exist, add it first.
            auto entry = table->newEntry();
            entry->setField(DAG_TRANSFER_FIELD_NAME, user);
            entry->setField(DAG_TRANSFER_FIELD_BALANCE, dev::u256(0).str());

            auto count = table->insert(user, entry, getOptions(origin));
            if (count == CODE_NO_AUTHORIZED)
            {  // permission denied
                strErrorMsg = "non-authorized";
                break;
            }
            toUserBalance = 0;
        }
        else
        {
            toUserBalance = dev::u256(entries->get(0)->getField(DAG_TRANSFER_FIELD_BALANCE)));
        }

        // overflow check
        if (toUserBalance + amount < toUserBalance)
        {
            strErrorMsg = "to user balance overflow.";
            break;
        }

        newFromUserBalance = fromUserBalance - amount;
        newToUserBalance = toUserBalance + amount;

        // update fromUser balance info.
        auto entry = table->newEntry();
        entry->setField(DAG_TRANSFER_FIELD_NAME, fromUser);
        entry->setField(DAG_TRANSFER_FIELD_BALANCE, newFromUserBalance.str());
        auto count = table->update(fromUser, entry, getOptions(origin));
        if (count == CODE_NO_AUTHORIZED)
        {  // permission denied
            strErrorMsg = "non-authorized";
            break;
        }

        // update toUser balance info.
        entry = table->newEntry();
        entry->setField(DAG_TRANSFER_FIELD_NAME, toUser);
        entry->setField(DAG_TRANSFER_FIELD_BALANCE, toFromUserBalance.str());
        count = table->update(toUser, entry, getOptions(origin));

        // end with success
        result = true;

    } while (0);

    if (result)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("DagTransferPrecompiled")
                               << LOG_DESC("userTransferCall") << LOG_KV("fromUser", fromUser)
                               << LOG_KV("toUser", toUser) << LOG_KV("amount", amount)
                               << LOG_KV("fromUserBalance", fromUserBalance)
                               << LOG_KV("toUserBalance", toUserBalance)
                               << LOG_KV("newFromUserBalance", newFromUserBalance)
                               << LOG_KV("newToUserBalance", newToUserBalance);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("DagTransferPrecompiled")
                               << LOG_DESC("userTransferCall") << LOG_KV("fromUser", fromUser)
                               << LOG_KV("toUser", toUser) << LOG_KV("amount", amount)
                               << LOG_KV("fromUserBalance", fromUserBalance)
                               << LOG_KV("toUserBalance", toUserBalance)
                               << LOG_KV("newFromUserBalance", newFromUserBalance)
                               << LOG_KV("newToUserBalance", newToUserBalance)
                               << LOG_DESC(strErrorMsg);
    }

    out = abi.abiIn("", result);
}