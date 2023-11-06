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
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    // [tableName][actualParams]
    std::vector<std::string> dynamicParams;
    bytes param;
    codec.decode(_callParameters->input(), dynamicParams, param);
    auto originParam = ref(param);
    uint32_t func = getParamFunc(originParam);

    // uint32_t func = getParamFunc(_callParameters->input());

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

    PRECOMPILED_LOG(INFO) << BLOCK_NUMBER(blockContext.number()) << LOG_BADGE("BalancePrecompiled")
                          << LOG_DESC("getBalance") << LOG_KV("account", accountStr);
    auto newParams = codec.encode("getAccountBalance", accountStr);
    auto getBalanceResult = externalRequest(_executive, ref(newParams), _callParameters->m_origin,
        _callParameters->m_codeAddress, accountStr, _callParameters->m_staticCall,
        _callParameters->m_create, _callParameters->m_gasLeft);
    // if getBalanceResult is 0, it means the account balance is not exist
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

    PRECOMPILED_LOG(TRACE) << BLOCK_NUMBER(blockContext.number()) << LOG_BADGE("BalancePrecompiled")
                           << LOG_DESC("getBalance") << LOG_KV("account", accountStr)
                           << LOG_KV("value", value);
    // check the sender whether belong to callers
    auto caller = _callParameters->m_origin;
    auto table = _executive->storage().openTable(SYS_BALANCE_CALLER);
    auto entry = table->getRow(caller);
    if (!entry.has_value() || entry->get() == "0")
    {
        _callParameters->setExecResult(codec.encode(int32_t(CODE_CHECK_CALLER_FAILED)));
        BOOST_THROW_EXCEPTION(protocol::PrecompiledError("caller not exist"));
    }

    //  addAccountBalance
    auto newParams = codec.encode("addAccountBalance(uint256)", accountStr, value);
    auto addBalanceResult = externalRequest(_executive, ref(newParams), _callParameters->m_origin,
        _callParameters->m_codeAddress, accountStr, _callParameters->m_staticCall,
        _callParameters->m_create, _callParameters->m_gasLeft);


    _callParameters->setExternalResult(std::move(addBalanceResult));
}
void BalancePrecompiled::subBalance(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    PrecompiledExecResult::Ptr const& _callParameters)
{
    // subBalance
    Address account;
    u256 value;
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    codec.decode(_callParameters->params(), account, value);
    std::string accountStr = account.hex();

    // check the sender whether belong to callers
    auto caller = _callParameters->m_origin;
    auto table = _executive->storage().openTable(SYS_BALANCE_CALLER);
    auto entry = table->getRow(caller);
    if (!entry.has_value() || entry->get() == "0")
    {
        _callParameters->setExecResult(codec.encode(int32_t(CODE_CHECK_CALLER_FAILED)));
        BOOST_THROW_EXCEPTION(protocol::PrecompiledError("caller not exist"));
    }

    //  subAccountBalance
    auto newParams = codec.encode("subAccountBalance(uint256)", accountStr, value);
    auto subBalanceResult = externalRequest(_executive, ref(newParams), _callParameters->m_origin,
        _callParameters->m_codeAddress, accountStr, _callParameters->m_staticCall,
        _callParameters->m_create, _callParameters->m_gasLeft);

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
    auto entry = table->getRow(caller);
    if (!entry || entry->get() == "0")
    {
        _callParameters->setExecResult(codec.encode(int32_t(CODE_CHECK_CALLER_FAILED)));
        BOOST_THROW_EXCEPTION(protocol::PrecompiledError("caller not exist"));
    }


    // first subAccountBalance, then addAccountBalance
    auto newParams = codec.encode("subAccountBalance(uint256)", fromStr, value);
    auto subBalanceResult = externalRequest(_executive, ref(newParams), _callParameters->m_origin,
        _callParameters->m_codeAddress, fromStr, _callParameters->m_staticCall,
        _callParameters->m_create, _callParameters->m_gasLeft);

    if (subBalanceResult->status == int32_t(CODE_SUCCESS))
    {
        newParams = codec.encode("addAccountBalance(uint256)", toStr, value);
        auto addBalanceResult = externalRequest(_executive, ref(newParams),
            _callParameters->m_origin, _callParameters->m_codeAddress, toStr,
            _callParameters->m_staticCall, _callParameters->m_create, _callParameters->m_gasLeft);
        _callParameters->setExternalResult(std::move(addBalanceResult));
    }
    else
    {
        _callParameters->setExecResult(codec.encode(int32_t(CODE_ACCOUNT_SUB_BALANCE_FAILED)));
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
    std::string accountStr = account.hex();
    // check is governor
    auto governors = getGovernorList(_executive, _callParameters, codec);
    if (RANGES::find(governors, account) == governors.end())
    {
        _callParameters->setExecResult(codec.encode(int32_t(CODE_REGISTER_CALLER_FAILED)));
        BOOST_THROW_EXCEPTION(protocol::PrecompiledError("only governor can register caller"));
    }

    // check the sender whether belong to callers
    auto table = _executive->storage().openTable(SYS_BALANCE_CALLER);
    if (!table)
    {
        std::string tableStr(SYS_BALANCE_CALLER);
        _executive->storage().createTable(tableStr, "1");
        Entry CallerEntry;
        CallerEntry.importFields({accountStr});
        _executive->storage().setRow(SYS_BALANCE_CALLER, accountStr, std::move(CallerEntry));
        _callParameters->setExecResult(codec.encode(int32_t(CODE_SUCCESS)));
    }
    {
        auto callerEntry = table->getRow(accountStr);
        if (callerEntry)
        {
            _callParameters->setExecResult(
                codec.encode(int32_t(CODE_REGISTER_CALLER_ALREADY_EXIST)));
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
    auto governors = getGovernorList(_executive, _callParameters, codec);
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
    if (!entry)
    {
        _callParameters->setExecResult(codec.encode(int32_t(CODE_REGISTER_CALLER_NOT_EXIST)));
        BOOST_THROW_EXCEPTION(protocol::PrecompiledError("caller not exist"));
    }
    if (RANGES::find(governors, account) == governors.end() || entry->get() != "0")
    {
        _callParameters->setExecResult(codec.encode(int32_t(CODE_UNREGISTER_CALLER_FAILED)));
        BOOST_THROW_EXCEPTION(
            protocol::PrecompiledError("only governor and caller self can unregister caller"));
    }

    // unregister callers, set value to 0
    Entry caller;
    caller.importFields({"0"});
    _executive->storage().setRow(SYS_BALANCE_CALLER, accountStr, std::move(caller));
    _callParameters->setExecResult(codec.encode(int32_t(CODE_SUCCESS)));

    PRECOMPILED_LOG(INFO) << BLOCK_NUMBER(blockContext.number()) << LOG_BADGE("BalancePrecompiled")
                          << LOG_DESC("unregisterCaller success") << LOG_KV("account", accountStr);
}
