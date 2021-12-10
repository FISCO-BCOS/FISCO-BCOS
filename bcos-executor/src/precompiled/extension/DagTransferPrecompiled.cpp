/**
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file DagTransferPrecompiled.cpp
 * @author: kyonRay
 * @date 2021-05-30
 */

#include "DagTransferPrecompiled.h"
#include "../PrecompiledCodec.h"
#include "../PrecompiledResult.h"
#include "../TableFactoryPrecompiled.h"
#include "../Utilities.h"

using namespace bcos;
using namespace bcos::executor;
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
const char* const DAG_TRANSFER = "dag_transfer";
const char* const DAG_TRANSFER_METHOD_ADD_STR_UINT = "userAdd(string,uint256)";
const char* const DAG_TRANSFER_METHOD_SAV_STR_UINT = "userSave(string,uint256)";
const char* const DAG_TRANSFER_METHOD_DRAW_STR_UINT = "userDraw(string,uint256)";
const char* const DAG_TRANSFER_METHOD_TRS_STR2_UINT = "userTransfer(string,string,uint256)";
const char* const DAG_TRANSFER_METHOD_BAL_STR = "userBalance(string)";

// fields of table '_dag_transfer_'
// const char* const DAG_TRANSFER_FIELD_NAME = "user_name";
const size_t DAG_TRANSFER_FIELD_BALANCE = 0;

DagTransferPrecompiled::DagTransferPrecompiled(crypto::Hash::Ptr _hashImpl) : Precompiled(_hashImpl)
{
    name2Selector[DAG_TRANSFER_METHOD_ADD_STR_UINT] =
        getFuncSelector(DAG_TRANSFER_METHOD_ADD_STR_UINT, _hashImpl);
    name2Selector[DAG_TRANSFER_METHOD_SAV_STR_UINT] =
        getFuncSelector(DAG_TRANSFER_METHOD_SAV_STR_UINT, _hashImpl);
    name2Selector[DAG_TRANSFER_METHOD_DRAW_STR_UINT] =
        getFuncSelector(DAG_TRANSFER_METHOD_DRAW_STR_UINT, _hashImpl);
    name2Selector[DAG_TRANSFER_METHOD_TRS_STR2_UINT] =
        getFuncSelector(DAG_TRANSFER_METHOD_TRS_STR2_UINT, _hashImpl);
    name2Selector[DAG_TRANSFER_METHOD_BAL_STR] =
        getFuncSelector(DAG_TRANSFER_METHOD_BAL_STR, _hashImpl);
}

std::vector<std::string> DagTransferPrecompiled::getParallelTag(bytesConstRef _param, bool _isWasm)
{
    // parse function name
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);

    std::vector<std::string> results;
    auto codec = std::make_shared<PrecompiledCodec>(m_hashImpl, _isWasm);
    // user_name user_balance 2 fields in table, the key of table is user_name field
    if (func == name2Selector[DAG_TRANSFER_METHOD_ADD_STR_UINT])
    {  // userAdd(string,uint256)
        std::string user;
        u256 amount;
        codec->decode(data, user, amount);
        // if params is invalid , parallel process can be done
        if (!user.empty())
        {
            results.push_back(user);
        }
    }
    else if (func == name2Selector[DAG_TRANSFER_METHOD_SAV_STR_UINT])
    {  // userSave(string,uint256)
        std::string user;
        u256 amount;
        codec->decode(data, user, amount);
        // if params is invalid , parallel process can be done
        if (!user.empty())
        {
            results.push_back(user);
        }
    }
    else if (func == name2Selector[DAG_TRANSFER_METHOD_DRAW_STR_UINT])
    {  // userDraw(string,uint256)
        std::string user;
        u256 amount;
        codec->decode(data, user, amount);
        // if params is invalid , parallel process can be done
        if (!user.empty())
        {
            results.push_back(user);
        }
    }
    else if (func == name2Selector[DAG_TRANSFER_METHOD_TRS_STR2_UINT])
    {
        // userTransfer(string,string,uint256)
        std::string fromUser, toUser;
        u256 amount;
        codec->decode(data, fromUser, toUser, amount);
        // if params is invalid , parallel process can be done
        if (!fromUser.empty() && !toUser.empty())
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

std::optional<storage::Table> DagTransferPrecompiled::openTable(
    std::shared_ptr<executor::TransactionExecutive> _executive)
{
    std::string dagTableName = precompiled::getTableName(DAG_TRANSFER);
    auto table = _executive->storage().openTable(dagTableName);
    if (!table)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("DagTransferPrecompiled")
                               << LOG_DESC("openTable: ready to create table")
                               << LOG_KV("tableName", dagTableName);
        //__dag_transfer__ is not exist, then create it first.
        table = _executive->storage().createTable(dagTableName, "balance");
        // table already exists
        if (!table)
        {
            PRECOMPILED_LOG(DEBUG)
                << LOG_BADGE("DagTransferPrecompiled") << LOG_DESC("table already exist")
                << LOG_KV("tableName", dagTableName);
            // try to openTable and get the table again
            table = _executive->storage().openTable(dagTableName);
        }
    }
    return table;
}

std::shared_ptr<PrecompiledExecResult> DagTransferPrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef _param,
    const std::string& _origin, const std::string&)
{
    // parse function name
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);
    auto callResult = std::make_shared<PrecompiledExecResult>();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();

    // user_name user_balance 2 fields in table, the key of table is user_name field
    if (func == name2Selector[DAG_TRANSFER_METHOD_ADD_STR_UINT])
    {  // userAdd(string,uint256)
        userAddCall(_executive, data, _origin, callResult->mutableExecResult());
    }
    else if (func == name2Selector[DAG_TRANSFER_METHOD_SAV_STR_UINT])
    {  // userSave(string,uint256)
        userSaveCall(_executive, data, _origin, callResult->mutableExecResult());
    }
    else if (func == name2Selector[DAG_TRANSFER_METHOD_DRAW_STR_UINT])
    {  // userDraw(string,uint256)
        userDrawCall(_executive, data, _origin, callResult->mutableExecResult());
    }
    else if (func == name2Selector[DAG_TRANSFER_METHOD_TRS_STR2_UINT])
    {  // userTransfer(string,string,uint256)
        userTransferCall(_executive, data, _origin, callResult->mutableExecResult());
    }
    else if (func == name2Selector[DAG_TRANSFER_METHOD_BAL_STR])
    {  // userBalance(string user)
        userBalanceCall(_executive, data, callResult->mutableExecResult());
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("DagTransferPrecompiled") << LOG_DESC("error func")
                               << LOG_KV("func", func);
    }
    gasPricer->updateMemUsed(callResult->m_execResult.size());
    callResult->setGas(gasPricer->calTotalGas());
    return callResult;
}

void DagTransferPrecompiled::userAddCall(std::shared_ptr<executor::TransactionExecutive> _executive,
    bytesConstRef _data, std::string const&, bytes& _out)
{
    // userAdd(string,uint256)
    std::string user;
    u256 amount;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(_data, user, amount);

    int ret;
    std::string strErrorMsg;
    do
    {
        if (user.empty())
        {
            strErrorMsg = "invalid user name";
            ret = CODE_INVALID_USER_NAME;
            break;
        }
        auto table = openTable(_executive);
        if (!table)
        {
            strErrorMsg = "openTable failed.";
            ret = CODE_INVALID_OPENTABLE_FAILED;
            break;
        }
        auto entry = table->getRow(user);
        if (entry)
        {
            strErrorMsg = "user already exist";
            ret = CODE_INVALID_USER_ALREADY_EXIST;
            break;
        }

        // user not exist, insert user into it.
        auto newEntry = table->newEntry();
        newEntry.setField(DAG_TRANSFER_FIELD_BALANCE, amount.str());
        table->setRow(user, newEntry);
        ret = 0;
    } while (false);
    if (!strErrorMsg.empty())
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("DagTransferPrecompiled") << LOG_DESC(strErrorMsg)
                               << LOG_KV("errorCode", ret);
    }
    _out = codec->encode(u256(ret));
}

void DagTransferPrecompiled::userSaveCall(
    std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef _data,
    std::string const&, bytes& _out)
{
    // userSave(string,uint256)
    std::string user;
    u256 amount;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(_data, user, amount);

    int ret;
    u256 balance;
    std::string strErrorMsg;
    do
    {
        if (user.empty())
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

        auto table = openTable(_executive);
        if (!table)
        {
            strErrorMsg = "openTable failed.";
            ret = CODE_INVALID_OPENTABLE_FAILED;
            break;
        }

        auto entry = table->getRow(user);
        if (!entry)
        {
            // If user is not exist, insert it. With this strategy, we can also add user by save
            // operation.
            auto newEntry = table->newEntry();
            newEntry.setField(DAG_TRANSFER_FIELD_BALANCE, amount.str());
            table->setRow(user, newEntry);
        }
        else
        {
            balance = u256(entry->getField(DAG_TRANSFER_FIELD_BALANCE));

            // if overflow
            auto new_balance = balance + amount;
            if (new_balance < balance)
            {
                strErrorMsg = "save overflow";
                ret = CODE_INVALID_BALANCE_OVERFLOW;
                break;
            }

            auto updateEntry = table->newEntry();
            updateEntry.setField(DAG_TRANSFER_FIELD_BALANCE, new_balance.str());
            table->setRow(user, updateEntry);
        }

        ret = 0;
    } while (false);
    if (!strErrorMsg.empty())
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("DagTransferPrecompiled") << LOG_DESC(strErrorMsg)
                               << LOG_KV("errorCode", ret);
    }
    _out = codec->encode(u256(ret));
}

void DagTransferPrecompiled::userDrawCall(
    std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef _data,
    std::string const&, bytes& _out)
{
    std::string user;
    u256 amount;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(_data, user, amount);

    u256 balance;
    int ret;
    std::string strErrorMsg;
    do
    {
        if (user.empty())
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
        auto table = openTable(_executive);
        if (!table)
        {
            strErrorMsg = "openTable failed.";
            ret = CODE_INVALID_OPENTABLE_FAILED;
            break;
        }

        auto entry = table->getRow(user);
        if (!entry)
        {
            strErrorMsg = "user not exist";
            ret = CODE_INVALID_USER_NOT_EXIST;
            break;
        }

        // only one record for every user
        balance = u256(entry->getField(DAG_TRANSFER_FIELD_BALANCE));
        if (balance < amount)
        {
            strErrorMsg = "insufficient balance";
            ret = CODE_INVALID_INSUFFICIENT_BALANCE;
            break;
        }

        auto new_balance = balance - amount;
        auto newEntry = table->newEntry();
        newEntry.setField(DAG_TRANSFER_FIELD_BALANCE, new_balance.str());
        table->setRow(user, newEntry);
        ret = 0;
    } while (false);
    if (!strErrorMsg.empty())
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("DagTransferPrecompiled") << LOG_DESC(strErrorMsg)
                               << LOG_KV("errorCode", ret);
    }
    _out = codec->encode(u256(ret));
}

void DagTransferPrecompiled::userBalanceCall(
    std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef _data, bytes& _out)
{
    std::string user;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(_data, user);

    u256 balance;
    int ret;
    std::string strErrorMsg;

    do
    {
        if (user.empty())
        {
            strErrorMsg = " invalid user name";
            ret = CODE_INVALID_USER_NAME;
            break;
        }

        auto table = openTable(_executive);
        if (!table)
        {
            strErrorMsg = "openTable failed.";
            ret = CODE_INVALID_OPENTABLE_FAILED;
            break;
        }

        auto entry = table->getRow(user);
        if (!entry)
        {
            strErrorMsg = "user not exist";
            ret = CODE_INVALID_USER_NOT_EXIST;
            break;
        }

        // only one record for every user
        balance = u256(entry->getField(DAG_TRANSFER_FIELD_BALANCE));
        ret = 0;
    } while (false);
    if (!strErrorMsg.empty())
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("DagTransferPrecompiled") << LOG_DESC(strErrorMsg)
                               << LOG_KV("errorCode", ret);
    }
    _out = codec->encode(u256(ret), balance);
}

void DagTransferPrecompiled::userTransferCall(
    std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef _data,
    std::string const&, bytes& _out)
{
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
    std::string fromUser, toUser;
    u256 amount;
    codec->decode(_data, fromUser, toUser, amount);

    u256 fromUserBalance, newFromUserBalance;
    u256 toUserBalance, newToUserBalance;

    std::string strErrorMsg;
    int ret;
    do
    {
        // parameters check
        if (fromUser.empty() || toUser.empty())
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
        auto table = openTable(_executive);
        if (!table)
        {
            strErrorMsg = "openTable failed.";
            ret = CODE_INVALID_OPENTABLE_FAILED;
            break;
        }

        auto entry = table->getRow(fromUser);
        if (!entry)
        {
            strErrorMsg = "from user not exist";
            ret = CODE_INVALID_USER_NOT_EXIST;
            break;
        }

        fromUserBalance = u256(entry->getField(DAG_TRANSFER_FIELD_BALANCE));
        if (fromUserBalance < amount)
        {
            strErrorMsg = "from user insufficient balance";
            ret = CODE_INVALID_INSUFFICIENT_BALANCE;
            break;
        }

        entry = table->getRow(toUser);
        if (!entry)
        {
            // If to user not exist, add it first.
            auto newEntry = table->newEntry();
            newEntry.setField(DAG_TRANSFER_FIELD_BALANCE, u256(0).str());
            table->setRow(toUser, newEntry);
            toUserBalance = 0;
        }
        else
        {
            toUserBalance = u256(entry->getField(DAG_TRANSFER_FIELD_BALANCE));
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
        entry = table->newEntry();
        entry->setField(DAG_TRANSFER_FIELD_BALANCE, newFromUserBalance.str());
        table->setRow(fromUser, *entry);

        // update toUser balance info.
        entry = table->newEntry();
        entry->setField(DAG_TRANSFER_FIELD_BALANCE, newToUserBalance.str());
        table->setRow(toUser, *entry);
        // end with success
        ret = 0;
    } while (false);
    if (!strErrorMsg.empty())
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("DagTransferPrecompiled") << LOG_DESC(strErrorMsg)
                               << LOG_KV("errorCode", ret);
    }
    _out = codec->encode(u256(ret));
}
