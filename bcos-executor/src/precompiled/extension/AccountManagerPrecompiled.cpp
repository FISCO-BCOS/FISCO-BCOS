/**
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @file AccountManagerPrecompiled.cpp
 * @author: kyonGuo
 * @date 2022/9/26
 */

#include "AccountManagerPrecompiled.h"
#include "../../vm/HostContext.h"
#include "bcos-executor/src/precompiled/common/Common.h"
#include "bcos-executor/src/precompiled/common/PrecompiledResult.h"
#include "bcos-executor/src/precompiled/common/Utilities.h"
#include <bcos-framework/protocol/Exceptions.h>
#include <boost/algorithm/string/classification.hpp>
#include <boost/throw_exception.hpp>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::protocol;

const char* const AM_METHOD_SET_ACCOUNT_STATUS = "setAccountStatus(address,uint8)";
const char* const AM_METHOD_GET_ACCOUNT_STATUS = "getAccountStatus(address)";

AccountManagerPrecompiled::AccountManagerPrecompiled(crypto::Hash::Ptr hashImpl)
  : Precompiled(hashImpl)
{
    name2Selector[AM_METHOD_SET_ACCOUNT_STATUS] =
        getFuncSelector(AM_METHOD_SET_ACCOUNT_STATUS, hashImpl);
    name2Selector[AM_METHOD_GET_ACCOUNT_STATUS] =
        getFuncSelector(AM_METHOD_GET_ACCOUNT_STATUS, hashImpl);
}

std::shared_ptr<PrecompiledExecResult> AccountManagerPrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive,
    PrecompiledExecResult::Ptr _callParameters)
{
    // parse function name
    uint32_t func = getParamFunc(_callParameters->input());

    /// directly passthrough data to call
    if (func == name2Selector[AM_METHOD_SET_ACCOUNT_STATUS])
    {
        setAccountStatus(_executive, _callParameters);
    }
    else if (func == name2Selector[AM_METHOD_GET_ACCOUNT_STATUS])
    {
        getAccountStatus(_executive, _callParameters);
    }
    else
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("AccountManagerPrecompiled")
                              << LOG_DESC("call undefined function") << LOG_KV("func", func);
        BOOST_THROW_EXCEPTION(
            bcos::protocol::PrecompiledError{} << errinfo_comment("AccountManagerPrecompiled call undefined function!"));
    }
    return _callParameters;
}

void AccountManagerPrecompiled::createAccountWithStatus(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters, const CodecWrapper& codec,
    std::string_view accountHex, uint8_t status) const
{
    auto accountTableName = getAccountTableName(accountHex);

    // prefix + address + tableName
    std::string codeString = getDynamicPrecompiledCodeString(ACCOUNT_ADDRESS, accountTableName);
    auto input = codec.encode(accountTableName, codeString);

    auto response = externalRequest(_executive, ref(input), _callParameters->m_origin,
        _callParameters->m_codeAddress, accountHex, false, true, _callParameters->m_gasLeft, true,
        std::string(ACCOUNT_ABI));

    if (response->status != (int32_t)TransactionStatus::None)
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("AccountManagerPrecompiled")
                              << LOG_DESC("createAccount failed")
                              << LOG_KV("accountTableName", accountTableName)
                              << LOG_KV("status", response->status);
        BOOST_THROW_EXCEPTION(PrecompiledError{} << errinfo_comment("Create account error."));
    }

    auto newParams = codec.encodeWithSig("setAccountStatus(uint8)", status);
    auto setStatusRes = externalRequest(_executive, ref(newParams), _callParameters->m_origin,
        _callParameters->m_codeAddress, accountHex, _callParameters->m_staticCall,
        _callParameters->m_create, _callParameters->m_gasLeft);

    if (setStatusRes->status != (int32_t)TransactionStatus::None)
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("AccountManagerPrecompiled")
                              << LOG_DESC("set status failed")
                              << LOG_KV("accountTableName", accountTableName)
                              << LOG_KV("status", response->status);
        BOOST_THROW_EXCEPTION(PrecompiledError{} << errinfo_comment("Set account status failed."));
    }
    _callParameters->setExternalResult(std::move(setStatusRes));
}

void AccountManagerPrecompiled::setAccountStatus(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters) const
{
    // setAccountStatus(address,uint8)
    Address account;
    uint8_t status = 0;
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    codec.decode(_callParameters->params(), account, status);
    std::string accountStr = account.hex();

    PRECOMPILED_LOG(INFO) << BLOCK_NUMBER(blockContext.number())
                          << LOG_BADGE("AccountManagerPrecompiled") << LOG_DESC("setAccountStatus")
                          << LOG_KV("account", accountStr)
                          << LOG_KV("status", std::to_string(status));

    // check is governor
    auto governors = getGovernorList(_executive, _callParameters, codec);
    if (RANGES::find_if(governors, [&_callParameters](const Address& address) {
            return address.hex() == _callParameters->m_sender;
        }) == governors.end())
    {
        // not from governor
        getErrorCodeOut(_callParameters->mutableExecResult(), CODE_NO_AUTHORIZED, codec);
        return;
    }
    if (RANGES::find(governors, account) != governors.end())
    {
        // set governor's status
        PRECOMPILED_LOG(INFO) << BLOCK_NUMBER(blockContext.number())
                              << LOG_BADGE("AccountManagerPrecompiled")
                              << LOG_DESC("set governor's status") << LOG_KV("account", accountStr)
                              << LOG_KV("status", std::to_string(status));
        BOOST_THROW_EXCEPTION(PrecompiledError{} << errinfo_comment("Should not set governor's status."));
    }

    // check account exist, if not exist, create first
    auto accountTableName = getAccountTableName(account.hex());
    auto table = _executive->storage().openTable(accountTableName);
    if (!table)
    {
        auto appsAccountTableName = getContractTableName(executor::USER_APPS_PREFIX, account.hex());
        auto appsTable = _executive->storage().openTable(appsAccountTableName);
        if (appsTable)
        {
            PRECOMPILED_LOG(INFO)
                << BLOCK_NUMBER(blockContext.number()) << LOG_BADGE("AccountManagerPrecompiled")
                << LOG_DESC("account table already exist in /apps, maybe this is a contract.")
                << LOG_KV("account", accountStr) << LOG_KV("status", std::to_string(status));
            _callParameters->setExecResult(codec.encode(int32_t(CODE_ACCOUNT_ALREADY_EXIST)));
            return;
        }
        PRECOMPILED_LOG(INFO) << BLOCK_NUMBER(blockContext.number())
                              << LOG_BADGE("AccountManagerPrecompiled")
                              << LOG_DESC("setAccountStatus table not exist, create first")
                              << LOG_KV("account", accountStr)
                              << LOG_KV("status", std::to_string(status));
        // create table
        createAccountWithStatus(_executive, _callParameters, codec, accountStr, status);
        return;
    }

    // table must exist, then call
    auto newParams = codec.encodeWithSig("setAccountStatus(uint8)", status);
    auto response = externalRequest(_executive, ref(newParams), _callParameters->m_origin,
        _callParameters->m_codeAddress, accountStr, _callParameters->m_staticCall,
        _callParameters->m_create, _callParameters->m_gasLeft);
    _callParameters->setExternalResult(std::move(response));
}

void AccountManagerPrecompiled::getAccountStatus(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters) const
{
    // getAccountStatus(address)
    Address account;
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    codec.decode(_callParameters->params(), account);
    std::string accountStr = account.hex();

    PRECOMPILED_LOG(TRACE) << BLOCK_NUMBER(blockContext.number()) << LOG_BADGE("getAccountStatus")
                           << LOG_KV("account", accountStr);
    auto newParams = codec.encodeWithSig("getAccountStatus()");
    auto response = externalRequest(_executive, ref(newParams), _callParameters->m_origin,
        _callParameters->m_codeAddress, accountStr, _callParameters->m_staticCall,
        _callParameters->m_create, _callParameters->m_gasLeft);
    if (response->status != (int32_t)TransactionStatus::None)
    {
        // maybe this address not exist in chain, return normal by default
        _callParameters->setExecResult(codec.encode((uint8_t)0));
        return;
    }
    // NOTE: if the address not exist, and feature set bugfix_call_noaddr_return, it will return
    // SUCCESS status, so it should be add special logic here.
    if (blockContext.features().get(ledger::Features::Flag::bugfix_call_noaddr_return) &&
        response->status == 0 && !response->message.empty())
    {
        // if the address not exist, return normal by default
        _callParameters->setExecResult(codec.encode((uint8_t)0));
        return;
    }
    _callParameters->setExternalResult(std::move(response));
}
