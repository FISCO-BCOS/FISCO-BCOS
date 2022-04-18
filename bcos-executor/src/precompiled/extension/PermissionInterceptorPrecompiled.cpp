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
 * @file PermissionInterceptorPrecompiled.cpp
 * @author: kyonGuo
 * @date 2022/4/14
 */

#include "PermissionInterceptorPrecompiled.h"
#include "../Utilities.h"

namespace bcos::precompiled
{

PermissionInterceptorPrecompiled::PermissionInterceptorPrecompiled(crypto::Hash::Ptr _hashImpl)
  : PermissionPrecompiledInterface(_hashImpl)
{
    name2Selector[PER_METHOD_CREATE] = getFuncSelector(PER_METHOD_CREATE, _hashImpl);
    name2Selector[PER_METHOD_SEND] = getFuncSelector(PER_METHOD_SEND, _hashImpl);
}

std::shared_ptr<PrecompiledExecResult> PermissionInterceptorPrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive>, bytesConstRef _param, const std::string&,
    const std::string&, int64_t)
{
    /// TODO: check necessity

    // parse function name
    uint32_t func = getParamFunc(_param);
    // bytesConstRef data = getParamData(_param);
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("AuthManagerPrecompiled") << LOG_DESC("call")
                           << LOG_KV("func", func);
    auto callResult = std::make_shared<PrecompiledExecResult>();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();

    gasPricer->setMemUsed(_param.size());

    if (func == name2Selector[PER_METHOD_CREATE])
    {
        /// TODO: check necessity
    }
    else if (func == name2Selector[PER_METHOD_SEND])
    {
        /// TODO: check necessity
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("AuthManagerPrecompiled")
                               << LOG_DESC("call undefined function") << LOG_KV("func", func);
    }
    gasPricer->updateMemUsed(callResult->m_execResult.size());
    callResult->setGas(gasPricer->calTotalGas());
    return callResult;
}

PermissionRet::Ptr PermissionInterceptorPrecompiled::create(
    const std::string&, const std::string&, const std::string&, bytesConstRef)
{
    // TODO: check necessity
    return nullptr;
}

PermissionRet::Ptr PermissionInterceptorPrecompiled::sendTransaction(
    const std::string&, const std::string&, const std::string&, bytesConstRef)
{
    // TODO: check necessity
    return nullptr;
}

}  // namespace bcos::precompiled
