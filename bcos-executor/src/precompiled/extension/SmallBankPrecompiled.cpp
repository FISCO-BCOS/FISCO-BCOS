/**
 *  copyright (C) 2021 FISCO BCOS.
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
 * @file CpuHeavyPrecompiled.cpp
 * @author: wenlinli
 * @date 2021-03-17
 */

#include "SmallBankPrecompiled.h"
#include "DagTransferPrecompiled.h"
#include "../PrecompiledResult.h"
#include "../Utilities.h"
#include "../PrecompiledCodec.h"
#include "../TableFactoryPrecompiled.h"
#include "../../executive/BlockContext.h"
#include <bcos-framework/interfaces/ledger/LedgerTypeDef.h>
#include <bcos-framework/interfaces/storage/Common.h>


using namespace bcos;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::precompiled;
using namespace bcos::ledger;


const char* const SMALL_BANK_METHOD_ADD_STR_UINT = "updateBalance(string,uint256)";
const char* const SMALL_BANK_METHOD_TRS_STR2_UINT = "sendPayment(string,string,uint256)";
const size_t SMALLBANK_TRANSFER_FIELD_BALANCE = 0;

SmallBankPrecompiled::SmallBankPrecompiled(crypto::Hash::Ptr _hashImpl, std::string _tableName) : Precompiled(_hashImpl),m_tableName(_tableName) {
    name2Selector[SMALL_BANK_METHOD_ADD_STR_UINT] = 
    getFuncSelector(SMALL_BANK_METHOD_ADD_STR_UINT, _hashImpl);
    name2Selector[SMALL_BANK_METHOD_TRS_STR2_UINT] = 
    getFuncSelector(SMALL_BANK_METHOD_TRS_STR2_UINT, _hashImpl);


}


std::vector<std::string> SmallBankPrecompiled::getParallelTag(bytesConstRef _param, bool _isWasm)
{
    // parse function name
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);
    std::vector<std::string> results; 
    auto codec = std::make_shared<PrecompiledCodec>(m_hashImpl, _isWasm);

    // user_name user_balance 2 fields in table, the key of table is user_name field
    if (func == name2Selector[SMALL_BANK_METHOD_ADD_STR_UINT])
    {  // updateBalance(string,uint256)
        std::string user;
        u256 amount;
        codec->decode(data, user, amount);
        // if params is invalid , parallel process can be done
        if (!user.empty())
        {
            results.push_back(user);
        }
    }

    else if (func == name2Selector[SMALL_BANK_METHOD_TRS_STR2_UINT])
    {
        // sendPayment(string,string,uint256)
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
    std::cout << "SmallBank getParallelTag done." << std::endl;
    return results;
}

std::string SmallBankPrecompiled::toString() {
    return "SmallBank";
}

std::shared_ptr<PrecompiledExecResult> SmallBankPrecompiled::call(
        std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef _param,
        const std::string & _origin, const std::string &) {
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("SmallBankPrecompiled") << LOG_DESC("call")
                           << LOG_KV("param", toHexString(_param));
    // parse function name
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);
    auto callResult = std::make_shared<PrecompiledExecResult>();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();


    // user_name user_balance 2 fields in table, the key of table is user_name field
    if (func == name2Selector[SMALL_BANK_METHOD_ADD_STR_UINT])
    {  // updateBalance(string,uint256)
        updateBalanceCall(_executive, data, _origin, callResult->mutableExecResult());
    }
    else if (func == name2Selector[SMALL_BANK_METHOD_TRS_STR2_UINT])
    {  // sendPayment(string,string,uint256)
        sendPaymentCall(_executive, data, _origin, callResult->mutableExecResult());
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("SmallBankPrecompiled") << LOG_DESC("error func")
                               << LOG_KV("func", func);
    }

    callResult->setGas(gasPricer->calTotalGas());
    callResult->setExecResult(bytes());
    std::cout << "SmallBank Precompiled call done." << std::endl;
    return callResult;
}

void SmallBankPrecompiled::updateBalanceCall(std::shared_ptr<executor::TransactionExecutive> _executive,
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
        auto table = _executive->storage().openTable(m_tableName);
        std::cout << "SmallBank  ---------- updateBalancecall tableName is " << m_tableName << std::endl;
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
        newEntry.setField(SMALLBANK_TRANSFER_FIELD_BALANCE, amount.str());
        std::cout << "SmallBank  ---------- user message has insert tableName: " << m_tableName << ", userName is" << user << ", balance is " << amount.str() << std::endl;
        table->setRow(user, newEntry);
        ret = 0;
    } while (false);
    if (!strErrorMsg.empty())
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("SmallBankPrecompiled") << LOG_DESC(strErrorMsg)
                               << LOG_KV("errorCode", ret);
    }
    _out = codec->encode(u256(ret));
}

void SmallBankPrecompiled::sendPaymentCall(
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
        auto table = _executive->storage().openTable(m_tableName);
        std::cout << "SmallBank  ---------- sendPaymentCall tableName is " << m_tableName << std::endl;
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

        fromUserBalance = u256(entry->getField(SMALLBANK_TRANSFER_FIELD_BALANCE));
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
            newEntry.setField(SMALLBANK_TRANSFER_FIELD_BALANCE, u256(0).str());
            table->setRow(toUser, newEntry);
            toUserBalance = 0;
        }
        else
        {
            toUserBalance = u256(entry->getField(SMALLBANK_TRANSFER_FIELD_BALANCE));
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
        
        std::cout << "SmallBank  ---------- user transfer message has insert tableName: " << m_tableName << ", fromUser is" << fromUser << ",fromUserBalance is" << fromUserBalance
        <<", newFromUserBalance is "<< newFromUserBalance << ", toUser is" << toUser <<  ", toUserBalance is" << toUserBalance << ", newToUserBalance is "<< newToUserBalance << std::endl;
        // update fromUser balance info.
        entry = table->newEntry();
        entry->setField(SMALLBANK_TRANSFER_FIELD_BALANCE, newFromUserBalance.str());
        table->setRow(fromUser, *entry);

        // update toUser balance info.
        entry = table->newEntry();
        entry->setField(SMALLBANK_TRANSFER_FIELD_BALANCE, newToUserBalance.str());
        table->setRow(toUser, *entry);
        // end with success
        ret = 0;
    } while (false);
    if (!strErrorMsg.empty())
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("SmallBankPrecompiled") << LOG_DESC(strErrorMsg)
                               << LOG_KV("errorCode", ret);
    }
    _out = codec->encode(u256(ret));
}


