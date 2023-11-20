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
 * @file BalancePrecompiled.h
 * @author: wenlinli
 * @date 2023-10-26
 */

#include "BalancePrecompiled.h"
#include "AccountManagerPrecompiled.h"
#include "libinitializer/AuthInitializer.h"
#include <bcos-tool/BfsFileFactory.h>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/core/ignore_unused.hpp>
#include <boost/serialization/vector.hpp>

using namespace bcos;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::precompiled;
using namespace bcos::ledger;
using namespace bcos::protocol;

const char* const BALANCE_METHOD_GET_BALANCE = "getBalance(address)";
const char* const BALANCE_METHOD_ADD_BALANCE = "addBalance(address,uint256)";
const char* const BALANCE_METHOD_SUB_BALANCE = "subBalance(address,uint256)";
const char* const BALANCE_METHOD_TRANSFER = "transfer(address,address,uint256)";
const char* const BALANCE_METHOD_REGISTER_CALLER = "registerCaller(address)";
const char* const BALANCE_METHOD_UNREGISTER_CALLER = "unregisterCaller(address)";

BalancePrecompiled::BalancePrecompiled(crypto::Hash::Ptr _hashImpl) : Precompiled(_hashImpl)
{
    name2Selector[BALANCE_METHOD_GET_BALANCE] =
        getFuncSelector(BALANCE_METHOD_GET_BALANCE, _hashImpl);
    name2Selector[BALANCE_METHOD_ADD_BALANCE] =
        getFuncSelector(BALANCE_METHOD_ADD_BALANCE, _hashImpl);
    name2Selector[BALANCE_METHOD_SUB_BALANCE] =
        getFuncSelector(BALANCE_METHOD_SUB_BALANCE, _hashImpl);
    name2Selector[BALANCE_METHOD_TRANSFER] = getFuncSelector(BALANCE_METHOD_TRANSFER, _hashImpl);
    name2Selector[BALANCE_METHOD_REGISTER_CALLER] =
        getFuncSelector(BALANCE_METHOD_REGISTER_CALLER, _hashImpl);
    name2Selector[BALANCE_METHOD_UNREGISTER_CALLER] =
        getFuncSelector(BALANCE_METHOD_UNREGISTER_CALLER, _hashImpl);
}

std::shared_ptr<PrecompiledExecResult> BalancePrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive,
    PrecompiledExecResult::Ptr _callParameters)
{
    // parse function name
    uint32_t func = getParamFunc(_callParameters->input());

    /// directly passthrough data to call
    if (func == name2Selector[BALANCE_METHOD_GET_BALANCE])
    {
        getBalance(_executive, _callParameters);
    }
    else if (func == name2Selector[BALANCE_METHOD_ADD_BALANCE])
    {
        addBalance(_executive, _callParameters);
    }
    else if (func == name2Selector[BALANCE_METHOD_SUB_BALANCE])
    {
        subBalance(_executive, _callParameters);
    }
    else if (func == name2Selector[BALANCE_METHOD_TRANSFER])
    {
        transfer(_executive, _callParameters);
    }
    else if (func == name2Selector[BALANCE_METHOD_REGISTER_CALLER])
    {
        registerCaller(_executive, _callParameters);
    }
    else if (func == name2Selector[BALANCE_METHOD_UNREGISTER_CALLER])
    {
        unregisterCaller(_executive, _callParameters);
    }
    else
    {
        BOOST_THROW_EXCEPTION(protocol::PrecompiledError("invalid function selector"));
    }
    return _callParameters;
}

void BalancePrecompiled::getBalance(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    PrecompiledExecResult::Ptr const& _callParameters)
{
    // getBalance
    Address account;
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    codec.decode(_callParameters->params(), account);
    std::string accountStr = account.hex();

    PRECOMPILED_LOG(TRACE) << BLOCK_NUMBER(blockContext.number()) << LOG_BADGE("BalancePrecompiled")
                           << LOG_DESC("getBalance") << LOG_KV("account", accountStr);
    auto accountTableName = getContractTableName(executor::USER_APPS_PREFIX, accountStr);
    auto table = _executive->storage().openTable(accountTableName);
    if (!table)
    {
        _callParameters->setExecResult(codec.encode(int32_t(CODE_ACCOUNT_NOT_EXIST)));
        PRECOMPILED_LOG(ERROR) << BLOCK_NUMBER(blockContext.number())
                               << LOG_BADGE("BalancePrecompiled") << LOG_DESC("getBalance")
                               << LOG_KV("account", accountStr)
                               << LOG_KV("accountTableNotExist", "true");
        BOOST_THROW_EXCEPTION(protocol::PrecompiledError("account not exist, getBalance failed"));
        return;
    }
    // get balance from account table
    auto params = codec.encodeWithSig("getAccountBalance()");
    auto tableName = getContractTableName(executor::USER_APPS_PREFIX, accountStr);
    std::vector<std::string> tableNameVector = {tableName};
    auto params2 = codec.encode(tableNameVector, params);
    auto input = codec.encode(std::string(ACCOUNT_ADDRESS), params2);


    auto sender = blockContext.isWasm() ? BALANCE_PRECOMPILED_NAME : BALANCE_PRECOMPILED_ADDRESS;
    auto getBalanceResult = externalRequest(_executive, ref(input), _callParameters->m_origin,
        sender, accountStr, _callParameters->m_staticCall, _callParameters->m_create,
        _callParameters->m_gasLeft, true);
    // if getBalanceResult is 0, it means the account balance is not exist
    PRECOMPILED_LOG(DEBUG) << BLOCK_NUMBER(blockContext.number()) << LOG_BADGE("BalancePrecompiled")
                           << LOG_DESC("getBalance") << LOG_KV("account", accountStr)
                           << LOG_KV("getBalanceResult", getBalanceResult->status);
    _callParameters->setExternalResult(std::move(getBalanceResult));
}


void BalancePrecompiled::addBalance(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    PrecompiledExecResult::Ptr const& _callParameters)
{
    // getBalance
    Address account;
    u256 value;
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    codec.decode(_callParameters->params(), account, value);
    std::string accountStr = account.hex();

    // check the sender whether belong to callers
    auto caller = _callParameters->m_origin;
    auto table = _executive->storage().openTable(SYS_BALANCE_CALLER);

    // if caller table not exist, check caller failed, return error
    if (!table)
    {
        PRECOMPILED_LOG(ERROR) << BLOCK_NUMBER(blockContext.number())
                               << LOG_BADGE("BalancePrecompiled") << LOG_DESC("addBalance")
                               << LOG_KV("account", accountStr) << LOG_KV("value", value)
                               << LOG_KV("caller", caller) << LOG_KV("callerTableNotExist", "true");
        _callParameters->setExecResult(codec.encode(int32_t(CODE_CALLER_TABLE_NOT_EXIST)));
        BOOST_THROW_EXCEPTION(
            protocol::PrecompiledError("caller table not exist, addBalance failed"));
    }
    auto entry = _executive->storage().getRow(SYS_BALANCE_CALLER, caller);
    PRECOMPILED_LOG(DEBUG) << BLOCK_NUMBER(blockContext.number()) << LOG_BADGE("BalancePrecompiled")
                           << LOG_DESC("addBalance") << LOG_KV("account", accountStr)
                           << LOG_KV("value", value) << LOG_KV("caller", caller)
                           << LOG_KV("callerEntry", entry->get());
    if (!entry.has_value() || entry->get() == "0")
    {
        _callParameters->setExecResult(codec.encode(int32_t(CODE_CHECK_CALLER_FAILED)));
        BOOST_THROW_EXCEPTION(protocol::PrecompiledError("caller not exist, addBalance failed"));
    }

    // AccountPrecompiledAddress  + addAccountBalance(value), internal call

    auto balanceParams = codec.encodeWithSig("addAccountBalance(uint256)", value);
    auto tableName = getContractTableName(executor::USER_APPS_PREFIX, accountStr);
    std::vector<std::string> tableNameVector = {tableName};
    auto inputParams = codec.encode(tableNameVector, balanceParams);
    // ACCOUNT_ADDRESS + [tableName] + params
    auto internalParams = codec.encode(std::string(ACCOUNT_ADDRESS), inputParams);


    auto addBalanceResult = externalRequest(_executive, ref(internalParams),
        _callParameters->m_origin, _callParameters->m_codeAddress, accountStr,
        _callParameters->m_staticCall, _callParameters->m_create, _callParameters->m_gasLeft, true);
    _callParameters->setExternalResult(std::move(addBalanceResult));
}

void BalancePrecompiled::subBalance(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    PrecompiledExecResult::Ptr const& _callParameters)
{
    Address account;
    u256 value;
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    codec.decode(_callParameters->params(), account, value);
    std::string accountStr = account.hex();

    // check the sender whether belong to callers
    auto caller = _callParameters->m_origin;
    auto table = _executive->storage().openTable(SYS_BALANCE_CALLER);
    // if caller table not exist, check caller failed, return error
    if (!table)
    {
        PRECOMPILED_LOG(ERROR) << BLOCK_NUMBER(blockContext.number())
                               << LOG_BADGE("BalancePrecompiled") << LOG_DESC("subBalance")
                               << LOG_KV("account", accountStr) << LOG_KV("value", value)
                               << LOG_KV("caller", caller) << LOG_KV("callerTableNotExist", "true");
        _callParameters->setExecResult(codec.encode(int32_t(CODE_CALLER_TABLE_NOT_EXIST)));
        BOOST_THROW_EXCEPTION(
            protocol::PrecompiledError("caller table not exist, subBalance failed"));
        return;
    }
    auto entry = table->getRow(caller);
    if (!entry.has_value() || entry->get() == "0")
    {
        _callParameters->setExecResult(codec.encode(int32_t(CODE_CHECK_CALLER_FAILED)));
        BOOST_THROW_EXCEPTION(protocol::PrecompiledError("caller not exist, subBalance failed"));
    }


    // AccountPrecompiledAddress  + subAccountBalance(value), internal call
    auto balanceParams = codec.encodeWithSig("subAccountBalance(uint256)", value);
    auto tableName = getContractTableName(executor::USER_APPS_PREFIX, accountStr);
    std::vector<std::string> tableNameVector = {tableName};
    auto inputParams = codec.encode(tableNameVector, balanceParams);

    // ACCOUNT_ADDRESS + [tableName] + params
    auto internalParams = codec.encode(std::string(ACCOUNT_ADDRESS), inputParams);
    auto subBalanceResult = externalRequest(_executive, ref(internalParams),
        _callParameters->m_origin, _callParameters->m_codeAddress, accountStr,
        _callParameters->m_staticCall, _callParameters->m_create, _callParameters->m_gasLeft, true);

    _callParameters->setExternalResult(std::move(subBalanceResult));
}
void BalancePrecompiled::transfer(const std::shared_ptr<executor::TransactionExecutive>& _executive,
    PrecompiledExecResult::Ptr const& _callParameters)
{
    // transfer
    Address from;
    Address to;
    u256 value;
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    codec.decode(_callParameters->params(), from, to, value);
    std::string fromStr = from.hex();
    std::string toStr = to.hex();

    PRECOMPILED_LOG(TRACE) << BLOCK_NUMBER(blockContext.number()) << LOG_BADGE("BalancePrecompiled")
                           << LOG_DESC("transfer") << LOG_KV("from", fromStr) << LOG_KV("to", toStr)
                           << LOG_KV("value", value);
    // check the sender whether belong to callers
    auto caller = _callParameters->m_origin;
    auto table = _executive->storage().openTable(SYS_BALANCE_CALLER);
    // if caller table not exist, check caller failed, return error
    if (!table)
    {
        PRECOMPILED_LOG(ERROR) << BLOCK_NUMBER(blockContext.number())
                               << LOG_BADGE("BalancePrecompiled") << LOG_DESC("transfer")
                               << LOG_KV("from", fromStr) << LOG_KV("to", toStr)
                               << LOG_KV("value", value) << LOG_KV("caller", caller)
                               << LOG_KV("callerTableNotExist", "true");
        _callParameters->setExecResult(codec.encode(int32_t(CODE_CALLER_TABLE_NOT_EXIST)));
        BOOST_THROW_EXCEPTION(
            protocol::PrecompiledError("caller table not exist, transfer failed"));
        return;
    }
    auto entry = table->getRow(caller);
    if (!entry || entry->get() == "0")
    {
        _callParameters->setExecResult(codec.encode(int32_t(CODE_CHECK_CALLER_FAILED)));
        BOOST_THROW_EXCEPTION(protocol::PrecompiledError("caller not exist"));
    }


    // first subAccountBalance, then addAccountBalance

    auto params = codec.encodeWithSig("subAccountBalance(uint256)", value);
    auto formTableName = getContractTableName(executor::USER_APPS_PREFIX, fromStr);
    std::vector<std::string> fromTableNameVector = {formTableName};
    auto inputParams = codec.encode(fromTableNameVector, params);

    auto subParams = codec.encode(std::string(ACCOUNT_ADDRESS), inputParams);
    auto subBalanceResult = externalRequest(_executive, ref(subParams), _callParameters->m_origin,
        _callParameters->m_codeAddress, fromStr, _callParameters->m_staticCall,
        _callParameters->m_create, _callParameters->m_gasLeft, true);

    if (subBalanceResult->status == int32_t(CODE_SUCCESS))
    {
        auto params1 = codec.encodeWithSig("addAccountBalance(uint256)", value);
        auto toTableName = getContractTableName(executor::USER_APPS_PREFIX, toStr);
        std::vector<std::string> toTableNameVector = {toTableName};
        auto inputParams1 = codec.encode(toTableNameVector, params1);
        auto addParams = codec.encode(std::string(ACCOUNT_ADDRESS), inputParams1);

        auto addBalanceResult =
            externalRequest(_executive, ref(addParams), _callParameters->m_origin,
                _callParameters->m_codeAddress, toStr, _callParameters->m_staticCall,
                _callParameters->m_create, _callParameters->m_gasLeft, true);
        if (addBalanceResult->status == int32_t(CODE_SUCCESS))
        {
            _callParameters->setExternalResult(std::move(addBalanceResult));
        }
        // if addBalanceResult is not success, then revert the subBalance, and return the error
        else
        {
            auto revertParams = codec.encode(fromTableNameVector, params1);
            auto addParams1 = codec.encode(std::string(ACCOUNT_ADDRESS), revertParams);
            auto addBalanceResult1 =
                externalRequest(_executive, ref(addParams1), _callParameters->m_origin,
                    _callParameters->m_codeAddress, fromStr, _callParameters->m_staticCall,
                    _callParameters->m_create, _callParameters->m_gasLeft, true);
            if (addBalanceResult1->status == int32_t(CODE_SUCCESS))
            {
                _callParameters->setExecResult(codec.encode(int32_t(CODE_TRANSFER_FAILED)));
            }
        }
    }
    else
    {
        _callParameters->setExecResult(codec.encode(int32_t(CODE_TRANSFER_FAILED)));
        PRECOMPILED_LOG(ERROR) << BLOCK_NUMBER(blockContext.number())
                               << LOG_BADGE("BalancePrecompiled") << LOG_DESC("transfer")
                               << LOG_KV("from", fromStr) << LOG_KV("to", toStr)
                               << LOG_KV("value", value)
                               << LOG_KV("subBalanceResult", subBalanceResult);
    }
}

void BalancePrecompiled::registerCaller(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    PrecompiledExecResult::Ptr const& _callParameters)
{
    // registerCaller
    Address account;
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    codec.decode(_callParameters->params(), account);
    auto sender = _callParameters->m_sender;
    std::string accountStr = account.hex();
    PRECOMPILED_LOG(TRACE) << BLOCK_NUMBER(blockContext.number()) << LOG_BADGE("registerCaller")
                           << LOG_DESC("registerCaller") << LOG_KV("account", accountStr)
                           << LOG_KV("sender", sender);
    // check is governor
    auto governors = getGovernorList(_executive, _callParameters, codec);
    PRECOMPILED_LOG(TRACE) << BLOCK_NUMBER(blockContext.number()) << LOG_BADGE("registerCaller")
                           << LOG_KV("governors size", governors.size())
                           << LOG_KV("governors[0] address", governors[0].hex());

    if (RANGES::find(governors, Address(sender)) == governors.end())
    {
        _callParameters->setExecResult(codec.encode(int32_t(CODE_REGISTER_CALLER_FAILED)));
        PRECOMPILED_LOG(TRACE) << BLOCK_NUMBER(blockContext.number()) << LOG_BADGE("registerCaller")
                               << LOG_DESC("failed to register, only governor can register caller");
        BOOST_THROW_EXCEPTION(protocol::PrecompiledError("only governor can register caller"));
        return;
    }

    // check the sender whether belong to callers
    auto table = _executive->storage().openTable(SYS_BALANCE_CALLER);
    if (!table)
    {
        std::string tableStr(SYS_BALANCE_CALLER);
        _executive->storage().createTable(tableStr, "value");
        Entry CallerEntry;
        CallerEntry.importFields({"1"});
        _executive->storage().setRow(SYS_BALANCE_CALLER, accountStr, std::move(CallerEntry));
        auto entry = _executive->storage().getRow(SYS_BALANCE_CALLER, accountStr);
        PRECOMPILED_LOG(TRACE) << BLOCK_NUMBER(blockContext.number()) << LOG_BADGE("registerCaller")
                               << LOG_DESC("create caller table success")
                               << LOG_KV("caller", accountStr) << LOG_KV("entry", entry->get());
        _callParameters->setExecResult(codec.encode(int32_t(CODE_SUCCESS)));
        return;
    }
    else
    {
        auto callerEntry = table->getRow(accountStr);
        if (callerEntry->get() == "1")
        {
            _callParameters->setExecResult(
                codec.encode(int32_t(CODE_REGISTER_CALLER_ALREADY_EXIST)));
            BOOST_THROW_EXCEPTION(protocol::PrecompiledError("caller already exist"));
            return;
        }
        Entry CallerEntry;
        CallerEntry.importFields({"1"});
        _executive->storage().setRow(SYS_BALANCE_CALLER, accountStr, std::move(CallerEntry));
        _callParameters->setExecResult(codec.encode(int32_t(CODE_SUCCESS)));
    }
    PRECOMPILED_LOG(INFO) << BLOCK_NUMBER(blockContext.number()) << LOG_BADGE("BalancePrecompiled")
                          << LOG_DESC("registerCaller") << LOG_KV("account", accountStr);
}

void BalancePrecompiled::unregisterCaller(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    PrecompiledExecResult::Ptr const& _callParameters)
{
    // registerCaller
    Address account;
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    codec.decode(_callParameters->params(), account);
    std::string accountStr = account.hex();
    // check is governor
    auto sender = _callParameters->m_sender;
    auto governors = getGovernorList(_executive, _callParameters, codec);
    if (RANGES::find(governors, Address(sender)) == governors.end())
    {
        _callParameters->setExecResult(codec.encode(int32_t(CODE_REGISTER_CALLER_FAILED)));
        PRECOMPILED_LOG(TRACE) << BLOCK_NUMBER(blockContext.number()) << LOG_BADGE("registerCaller")
                               << LOG_DESC("failed to register, only governor can register caller");
        BOOST_THROW_EXCEPTION(protocol::PrecompiledError("only governor can register caller"));
        return;
    }

    auto table = _executive->storage().openTable(SYS_BALANCE_CALLER);
    if (!table)
    {
        std::string tableStr(SYS_BALANCE_CALLER);
        _executive->storage().createTable(tableStr, "caller_address");
        _callParameters->setExecResult(codec.encode(int32_t(CODE_UNREGISTER_CALLER_FAILED)));
        PRECOMPILED_LOG(ERROR) << BLOCK_NUMBER(blockContext.number())
                               << LOG_BADGE("BalancePrecompiled") << LOG_DESC("unregisterCaller")
                               << LOG_KV("account", accountStr);
    }
    auto entry = table->getRow(accountStr);
    if (!entry.has_value())
    {
        _callParameters->setExecResult(codec.encode(int32_t(CODE_REGISTER_CALLER_NOT_EXIST)));
        BOOST_THROW_EXCEPTION(protocol::PrecompiledError("caller not exist"));
    }

    // unregister callers, set value to 0
    Entry caller;
    caller.importFields({"0"});
    _executive->storage().setRow(SYS_BALANCE_CALLER, accountStr, std::move(caller));
    _callParameters->setExecResult(codec.encode(int32_t(CODE_SUCCESS)));

    PRECOMPILED_LOG(INFO) << BLOCK_NUMBER(blockContext.number()) << LOG_BADGE("BalancePrecompiled")
                          << LOG_DESC("unregisterCaller success") << LOG_KV("account", accountStr);
}