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
#include "bcos-framework/bcos-framework/storage/Table.h"
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
const char* const BALANCE_METHOD_LIST_CALLER = "listCaller()";

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
    name2Selector[BALANCE_METHOD_LIST_CALLER] =
        getFuncSelector(BALANCE_METHOD_LIST_CALLER, _hashImpl);
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
    else if (func == name2Selector[BALANCE_METHOD_LIST_CALLER])
    {
        listCaller(_executive, _callParameters);
    }
    else
    {
        BOOST_THROW_EXCEPTION(protocol::PrecompiledError("invalid function selector"));
    }
    return _callParameters;
}


std::string BalancePrecompiled::getContractTableName(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const std::string_view& _address)
{
    return _executive->getContractTableName(_address, _executive->isWasm(), false);
}

void BalancePrecompiled::checkOriginAuth(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    PrecompiledExecResult::Ptr const& _callParameters, const CodecWrapper& codec)
{
    auto origin = _callParameters->m_origin;
    auto governors = getGovernorList(_executive, _callParameters, codec);
    PRECOMPILED_LOG(TRACE) << BLOCK_NUMBER(_executive->blockContext().number())
                           << LOG_BADGE("BalancePrecompiled") << LOG_DESC("checkOriginAuth")
                           << LOG_KV("governors size", governors.size())
                           << LOG_KV("origin address", origin);
    if (RANGES::find(governors, Address(origin)) == governors.end())
    {
        PRECOMPILED_LOG(TRACE)
            << BLOCK_NUMBER(_executive->blockContext().number()) << LOG_BADGE("BalancePrecompiled")
            << LOG_DESC("checkOriginAuth, failed to register, only governor can register caller");
        BOOST_THROW_EXCEPTION(protocol::PrecompiledError("only governor can register caller"));
    }
}

void BalancePrecompiled::createAccount(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters, const bcos::CodecWrapper& codec,
    std::string_view accountHex)
{
    auto accountTableName = getAccountTableName(accountHex);

    // prefix + address + tableName
    std::string codeString = getDynamicPrecompiledCodeString(ACCOUNT_ADDRESS, accountTableName);
    auto input = codec.encode(accountTableName, codeString);

    auto response = externalRequest(_executive, ref(input), _callParameters->m_origin,
        _callParameters->m_codeAddress, accountHex, false, true, _callParameters->m_gasLeft, true);

    if (response->status != (int32_t)TransactionStatus::None)
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("AccountManagerPrecompiled")
                              << LOG_DESC("createAccount failed")
                              << LOG_KV("accountTableName", accountTableName)
                              << LOG_KV("status", response->status);
        BOOST_THROW_EXCEPTION(PrecompiledError("Create account error."));
    }
    return;
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

    // get balance from account table
    auto params = codec.encodeWithSig("getAccountBalance()");
    auto tableName = getContractTableName(_executive, accountStr);
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
    auto caller = _callParameters->m_sender;
    auto table = _executive->storage().openTable(SYS_BALANCE_CALLER);

    // if caller table not exist, check caller failed, return error
    if (!table)
    {
        PRECOMPILED_LOG(ERROR) << BLOCK_NUMBER(blockContext.number())
                               << LOG_BADGE("BalancePrecompiled") << LOG_DESC("addBalance")
                               << LOG_KV("account", accountStr) << LOG_KV("value", value)
                               << LOG_KV("caller", caller) << LOG_KV("callerTableNotExist", "true");
        BOOST_THROW_EXCEPTION(
            protocol::PrecompiledError("Permission denied. Please use \"listBalanceGovernor\" to "
                                       "check which account(or contract) can addBalance"));
    }
    auto entry = _executive->storage().getRow(SYS_BALANCE_CALLER, caller);
    if (!entry.has_value())
    {
        BOOST_THROW_EXCEPTION(
            protocol::PrecompiledError("Permission denied. Please use \"listBalanceGovernor\" to "
                                       "check which account(or contract) can addBalance"));
    }
    PRECOMPILED_LOG(DEBUG) << BLOCK_NUMBER(blockContext.number()) << LOG_BADGE("BalancePrecompiled")
                           << LOG_DESC("addBalance") << LOG_KV("account", accountStr)
                           << LOG_KV("value", value) << LOG_KV("caller", caller)
                           << LOG_KV("callerEntry", entry->get());

    // check the account whether exist, if not exist, create the account
    auto accountTableName = getContractTableName(_executive, accountStr);
    auto table1 = _executive->storage().openTable(accountTableName);
    if (!table1)
    {
        PRECOMPILED_LOG(DEBUG) << BLOCK_NUMBER(blockContext.number())
                               << LOG_BADGE("BalancePrecompiled, addBalance")
                               << LOG_DESC(
                                      "account not exist apps table, account maybe not contract "
                                      "account, create account")
                               << LOG_KV("account", accountStr) << LOG_KV("value", value)
                               << LOG_KV("caller", caller);
        createAccount(_executive, _callParameters, codec, accountStr);
    }

    // AccountPrecompiledAddress  + addAccountBalance(value), internal call
    auto balanceParams = codec.encodeWithSig("addAccountBalance(uint256)", value);
    std::vector<std::string> tableNameVector = {accountTableName};
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
    auto caller = _callParameters->m_sender;
    auto table = _executive->storage().openTable(SYS_BALANCE_CALLER);
    // if caller table not exist, check caller failed, return error
    if (!table)
    {
        PRECOMPILED_LOG(WARNING) << BLOCK_NUMBER(blockContext.number())
                                 << LOG_BADGE("BalancePrecompiled") << LOG_DESC("subBalance")
                                 << LOG_KV("account", accountStr) << LOG_KV("value", value)
                                 << LOG_KV("caller", caller)
                                 << LOG_KV("callerTableNotExist", "true");
        BOOST_THROW_EXCEPTION(
            protocol::PrecompiledError("Permission denied. Please use \"listBalanceGovernor\" to "
                                       "check which account(or contract) can subBalance"));
    }
    auto entry = table->getRow(caller);
    if (!entry.has_value())
    {
        BOOST_THROW_EXCEPTION(
            protocol::PrecompiledError("Permission denied. Please use \"listBalanceGovernor\" to "
                                       "check which account(or contract) can subBalance"));
    }

    // check the account whether exist, if not exist, create the account
    auto accountTableName = getContractTableName(_executive, accountStr);
    auto table1 = _executive->storage().openTable(accountTableName);
    if (!table1)
    {
        PRECOMPILED_LOG(DEBUG) << BLOCK_NUMBER(blockContext.number())
                               << LOG_BADGE("BalancePrecompiled, subBalance")
                               << LOG_DESC(
                                      "account not exist apps table, account maybe not contract "
                                      "account, create account")
                               << LOG_KV("account", accountStr) << LOG_KV("value", value)
                               << LOG_KV("caller", caller);
        createAccount(_executive, _callParameters, codec, accountStr);
    }

    // AccountPrecompiledAddress  + subAccountBalance(value), internal call
    auto balanceParams = codec.encodeWithSig("subAccountBalance(uint256)", value);
    auto tableName = getContractTableName(_executive, accountStr);
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
    auto caller = _callParameters->m_sender;
    auto table = _executive->storage().openTable(SYS_BALANCE_CALLER);
    // if caller table not exist, check caller failed, return error
    if (!table)
    {
        PRECOMPILED_LOG(WARNING) << BLOCK_NUMBER(blockContext.number())
                                 << LOG_BADGE("BalancePrecompiled") << LOG_DESC("transfer")
                                 << LOG_KV("from", fromStr) << LOG_KV("to", toStr)
                                 << LOG_KV("value", value) << LOG_KV("caller", caller)
                                 << LOG_KV("callerTableNotExist", "true");
        BOOST_THROW_EXCEPTION(
            protocol::PrecompiledError("Permission denied. Please use \"listBalanceGovernor\" to "
                                       "check which account(or contract) can transferBalance"));
    }
    auto entry = table->getRow(caller);
    if (!entry)
    {
        BOOST_THROW_EXCEPTION(
            protocol::PrecompiledError("Permission denied. Please use \"listBalanceGovernor\" to "
                                       "check which account(or contract) can transferBalance"));
    }
    // first subAccountBalance, then addAccountBalance

    auto params = codec.encodeWithSig("subAccountBalance(uint256)", value);
    auto formTableName = getContractTableName(_executive, fromStr);
    std::vector<std::string> fromTableNameVector = {formTableName};
    auto inputParams = codec.encode(fromTableNameVector, params);

    // check the from account whether exist, if not exist, create the account
    auto fromAccountTableName = getContractTableName(_executive, fromStr);
    auto fromTable = _executive->storage().openTable(fromAccountTableName);
    if (!fromTable)
    {
        PRECOMPILED_LOG(DEBUG) << BLOCK_NUMBER(blockContext.number())
                               << LOG_BADGE("BalancePrecompiled, transfer stage 1")
                               << LOG_DESC(
                                      "account not exist apps table, account maybe not contract "
                                      "account, create account")
                               << LOG_KV("fromStr", fromStr) << LOG_KV("value", value)
                               << LOG_KV("caller", caller);
        createAccount(_executive, _callParameters, codec, fromStr);
    }

    auto subParams = codec.encode(std::string(ACCOUNT_ADDRESS), inputParams);
    auto subBalanceResult = externalRequest(_executive, ref(subParams), _callParameters->m_origin,
        _callParameters->m_codeAddress, fromStr, _callParameters->m_staticCall,
        _callParameters->m_create, _callParameters->m_gasLeft, true);

    if (subBalanceResult->status == int32_t(CODE_SUCCESS))
    {
        auto params1 = codec.encodeWithSig("addAccountBalance(uint256)", value);
        auto toTableName = getContractTableName(_executive, toStr);
        std::vector<std::string> toTableNameVector = {toTableName};
        auto inputParams1 = codec.encode(toTableNameVector, params1);
        auto addParams = codec.encode(std::string(ACCOUNT_ADDRESS), inputParams1);

        // check the to account whether exist, if not exist, create the account
        auto toAccountTableName = getContractTableName(_executive, toStr);
        auto toTable = _executive->storage().openTable(toAccountTableName);
        if (!toTable)
        {
            PRECOMPILED_LOG(DEBUG)
                << BLOCK_NUMBER(blockContext.number())
                << LOG_BADGE("BalancePrecompiled, transfer stage 2")
                << LOG_DESC(
                       "account not exist apps table, account maybe not contract account, create "
                       "account")
                << LOG_KV("fromStr", fromStr) << LOG_KV("value", value) << LOG_KV("caller", caller);
            createAccount(_executive, _callParameters, codec, toStr);
        }

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
                BOOST_THROW_EXCEPTION(PrecompiledError("transfer failed"));
            }
        }
    }
    else
    {
        BOOST_THROW_EXCEPTION(protocol::PrecompiledError(
            "transfer failed, account subBalance failed, please check the account balance"));
        PRECOMPILED_LOG(WARNING) << BLOCK_NUMBER(blockContext.number())
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
    std::string accountStr = account.hex();
    // check is governor
    checkOriginAuth(_executive, _callParameters, codec);

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
        // check table size whether exceed the limit
        auto keyCondition = std::make_optional<storage::Condition>();
        keyCondition->limit(0, USER_TABLE_MAX_LIMIT_COUNT);
        auto tableKeyList = table->getPrimaryKeys(*keyCondition);
        if (tableKeyList.size() >= USER_TABLE_MAX_LIMIT_COUNT)
        {
            PRECOMPILED_LOG(TRACE)
                << BLOCK_NUMBER(blockContext.number()) << LOG_BADGE("registerCaller")
                << LOG_DESC("failed to register, caller table size exceed limit")
                << LOG_KV("caller", accountStr) << LOG_KV("tableKeyList size", tableKeyList.size());
            BOOST_THROW_EXCEPTION(protocol::PrecompiledError("caller table size exceed limit"));
        }
        auto callerEntry = table->getRow(accountStr);
        if (callerEntry)
        {
            BOOST_THROW_EXCEPTION(protocol::PrecompiledError("caller already exist"));
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
    checkOriginAuth(_executive, _callParameters, codec);
    auto table = _executive->storage().openTable(SYS_BALANCE_CALLER);
    if (!table)
    {
        std::string tableStr(SYS_BALANCE_CALLER);
        _executive->storage().createTable(tableStr, "value");
        PRECOMPILED_LOG(WARNING) << BLOCK_NUMBER(blockContext.number())
                                 << LOG_BADGE("BalancePrecompiled") << LOG_DESC("unregisterCaller")
                                 << LOG_KV("account", accountStr);
    }
    auto entry = table->getRow(accountStr);
    // check the caller whether is last entry, if it is, prohibit to unregister
    auto keyCondition = std::make_optional<storage::Condition>();
    keyCondition->limit(0, USER_TABLE_MAX_LIMIT_COUNT);
    auto tableKeyList = table->getPrimaryKeys(*keyCondition);
    if (tableKeyList.size() == 1)
    {
        PRECOMPILED_LOG(INFO) << BLOCK_NUMBER(blockContext.number())
                              << LOG_BADGE("BalancePrecompiled")
                              << LOG_DESC(
                                     "unregisterCaller failed, caller table only has one entry, "
                                     "prohibit to unregister")
                              << LOG_KV("account", accountStr);
        BOOST_THROW_EXCEPTION(protocol::PrecompiledError(
            "BalanceGovernor table only has one account, prohibit to unregister"));
    }
    if (!entry.has_value())
    {
        BOOST_THROW_EXCEPTION(protocol::PrecompiledError("caller not exist"));
    }

    // unregister callers, set entry to deleted state
    auto deletedEntry = std::make_optional(table->newDeletedEntry());
    table->setRow(accountStr, std::move(*deletedEntry));
    _callParameters->setExecResult(codec.encode(int32_t(CODE_SUCCESS)));

    PRECOMPILED_LOG(INFO) << BLOCK_NUMBER(blockContext.number()) << LOG_BADGE("BalancePrecompiled")
                          << LOG_DESC("unregisterCaller success") << LOG_KV("account", accountStr);
}

void BalancePrecompiled::listCaller(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters)
{
    // listCaller
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());

    auto table = _executive->storage().openTable(SYS_BALANCE_CALLER);
    if (!table)
    {
        PRECOMPILED_LOG(INFO) << BLOCK_NUMBER(blockContext.number())
                              << LOG_BADGE("BalancePrecompiled")
                              << LOG_DESC("listCaller failed, caller table not exist");
        BOOST_THROW_EXCEPTION(protocol::PrecompiledError("caller table not exist."));
    }
    auto keyCondition = std::make_optional<storage::Condition>();
    keyCondition->limit(0, USER_TABLE_MAX_LIMIT_COUNT);
    auto tableKeyList = table->getPrimaryKeys(*keyCondition);
    if (tableKeyList.size() == 0)
    {
        PRECOMPILED_LOG(INFO) << BLOCK_NUMBER(blockContext.number())
                              << LOG_BADGE("BalancePrecompiled")
                              << LOG_DESC("listCaller failed, caller table is empty");
        BOOST_THROW_EXCEPTION(protocol::PrecompiledError("caller table is empty."));
    }
    Addresses addresses;
    for (auto& it : tableKeyList)
    {
        auto entry = table->getRow(it);
        if (entry)
        {
            addresses.emplace_back(Address(it));
        }
    }
    PRECOMPILED_LOG(INFO) << BLOCK_NUMBER(blockContext.number()) << LOG_BADGE("BalancePrecompiled")
                          << LOG_DESC("listCaller success")
                          << LOG_KV("addresses size", addresses.size());
    auto encodeCallers = codec.encode(addresses);
    _callParameters->setExecResult(std::move(encodeCallers));
}