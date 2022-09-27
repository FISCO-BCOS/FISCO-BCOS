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

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;

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
    [[maybe_unused]] const std::shared_ptr<executor::TransactionExecutive>& _executive,
    [[maybe_unused]] const PrecompiledExecResult::Ptr& _callParameters)
{}

void AccountManagerPrecompiled::setAccountStatus(
    [[maybe_unused]] const std::shared_ptr<executor::TransactionExecutive>& _executive,
    [[maybe_unused]] const PrecompiledExecResult::Ptr& _callParameters)
{}

void AccountManagerPrecompiled::getAccountStatus(
    [[maybe_unused]] const std::shared_ptr<executor::TransactionExecutive>& _executive,
    [[maybe_unused]] const PrecompiledExecResult::Ptr& _callParameters)
{}

[[maybe_unused]] uint16_t AccountManagerPrecompiled::getAccountStatus(
    [[maybe_unused]] const std::shared_ptr<executor::TransactionExecutive>& _executive,
    [[maybe_unused]] const std::string& _address)
{
    return 0;
}
