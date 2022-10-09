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
#include <boost/algorithm/string/split.hpp>
#include <boost/throw_exception.hpp>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::protocol;

const char* const AM_METHOD_CREATE_ACCOUNT = "createAccount(address)";
const char* const AM_METHOD_SET_ACCOUNT_STATUS = "setAccountStatus(address,uint16)";
const char* const AM_METHOD_GET_ACCOUNT_STATUS = "getAccountStatus(address)";

AccountManagerPrecompiled::AccountManagerPrecompiled(crypto::Hash::Ptr _hashImpl)
  : Precompiled(_hashImpl)
{
    name2Selector[AM_METHOD_CREATE_ACCOUNT] = getFuncSelector(AM_METHOD_CREATE_ACCOUNT, _hashImpl);
    name2Selector[AM_METHOD_SET_ACCOUNT_STATUS] =
        getFuncSelector(AM_METHOD_SET_ACCOUNT_STATUS, _hashImpl);
    name2Selector[AM_METHOD_GET_ACCOUNT_STATUS] =
        getFuncSelector(AM_METHOD_GET_ACCOUNT_STATUS, _hashImpl);
}

std::shared_ptr<PrecompiledExecResult> AccountManagerPrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive,
    PrecompiledExecResult::Ptr _callParameters)
{
    // parse function name
    uint32_t func = getParamFunc(_callParameters->input());

    /// directly passthrough data to call
    if (func == name2Selector[AM_METHOD_CREATE_ACCOUNT])
    {
        createAccount(_executive, _callParameters);
    }
    else if (func == name2Selector[AM_METHOD_SET_ACCOUNT_STATUS])
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
            bcos::protocol::PrecompiledError("AccountManagerPrecompiled call undefined function!"));
    }
    return _callParameters;
}

void AccountManagerPrecompiled::createAccount(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters)
{
    // createAccount(address)
    Address account;
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    codec.decode(_callParameters->params(), account);

    PRECOMPILED_LOG(INFO) << BLOCK_NUMBER(blockContext->number())
                          << LOG_BADGE("AccountManagerPrecompiled")
                          << LOG_KV("createAccount", account.hex());
    if (!checkSenderFromAuth(_callParameters->m_sender))
    {
        getErrorCodeOut(_callParameters->mutableExecResult(), CODE_NO_AUTHORIZED, codec);
        return;
    }
    auto accountTableName = getAccountTableName(account.hex());
    auto table = _executive->storage().openTable(accountTableName);
    if (table)
    {
        // table already exist
        _callParameters->setExecResult(codec.encode(int32_t(CODE_ACCOUNT_ALREADY_EXIST)));
        return;
    }

    // prefix + address + tableName
    std::string codeString = getDynamicPrecompiledCodeString(ACCOUNT_ADDRESS, accountTableName);

    auto input = codec.encode(accountTableName, codeString);

    auto response = externalRequest(_executive, ref(input), _callParameters->m_origin,
        _callParameters->m_codeAddress, account.hex(), false, true, _callParameters->m_gas);

    if (response->status != (int32_t)TransactionStatus::None)
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("AccountManagerPrecompiled") << LOG_DESC("createAccount")
                              << LOG_KV("accountTableName", accountTableName)
                              << LOG_KV("status", response->status);
        BOOST_THROW_EXCEPTION(PrecompiledError("Create account error."));
    }

    Entry statusEntry;
    statusEntry.importFields({"0"});
    _executive->storage().setRow(accountTableName, ACCOUNT_STATUS, std::move(statusEntry));
    Entry updateEntry;
    updateEntry.importFields({std::to_string(blockContext->number())});
    _executive->storage().setRow(accountTableName, ACCOUNT_LAST_UPDATE, std::move(updateEntry));
    _callParameters->setExecResult(codec.encode(int32_t(CODE_SUCCESS)));
}

void AccountManagerPrecompiled::setAccountStatus(
    [[maybe_unused]] const std::shared_ptr<executor::TransactionExecutive>& _executive,
    [[maybe_unused]] const PrecompiledExecResult::Ptr& _callParameters)
{}

void AccountManagerPrecompiled::getAccountStatus(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters)
{
    // getAccountStatus(address)
    Address account;
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    codec.decode(_callParameters->params(), account);
    std::string accountStr = account.hex();

    PRECOMPILED_LOG(TRACE) << BLOCK_NUMBER(blockContext->number()) << LOG_BADGE("getAccountStatus")
                           << LOG_KV("account", accountStr);
    auto newParams = codec.encodeWithSig("getAccountStatus()");
    auto response = externalRequest(_executive, ref(newParams), _callParameters->m_origin,
        _callParameters->m_codeAddress, accountStr, _callParameters->m_staticCall,
        _callParameters->m_create, _callParameters->m_gas);
    _callParameters->setExternalResult(std::move(response));
}
