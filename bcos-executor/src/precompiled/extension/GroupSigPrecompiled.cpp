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
 * @file GroupSigPrecompiled.cpp
 * @author: yklu
 * @date 2022-04-12
 */

#include "GroupSigPrecompiled.h"
#include <group_sig/algorithm/GroupSig.h>
#include "../../executive/BlockContext.h"
#include "bcos-executor/src/precompiled/common/PrecompiledResult.h"
#include "bcos-executor/src/precompiled/common/Utilities.h"

using namespace bcos;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::precompiled;

/*
contract GroupSig
{
    function groupSigVerify(string signature, string message, string gpkInfo, string paramInfo)
public constant returns(bool);
}
*/

const char* const GroupSig_METHOD_SET_STR = "groupSigVerify(string,string,string,string)";

GroupSigPrecompiled::GroupSigPrecompiled(crypto::Hash::Ptr _hashImpl) : Precompiled(_hashImpl)
{
    name2Selector[GroupSig_METHOD_SET_STR] = getFuncSelector(GroupSig_METHOD_SET_STR, _hashImpl);
}

std::shared_ptr<PrecompiledExecResult> GroupSigPrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive,
    PrecompiledExecResult::Ptr _callParameters)
{
    // parse function name
    uint32_t func = getParamFunc(_callParameters->input());
    bytesConstRef data = _callParameters->params();
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<CodecWrapper>(blockContext->hashHandler(), blockContext->isWasm());
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();
    gasPricer->updateMemUsed(_callParameters->m_execResult.size());
    
    if (func == name2Selector[GroupSig_METHOD_SET_STR])
    {
        // groupSigVerify(string)
        std::string signature, message, gpkInfo, paramInfo;
        codec->decode(data, signature, message, gpkInfo, paramInfo);
        bool result = false;

        try
        {
            result = GroupSigApi::group_verify(signature, message, gpkInfo, paramInfo);
            gasPricer->appendOperation(InterfaceOpcode::GroupSigVerify);
        }
        catch (std::exception& error)
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("GroupSigPrecompiled") << LOG_DESC(errorMsg)
                                   << LOG_KV("signature", signature) << LOG_KV("message", message)
                                   << LOG_KV("gpkInfo", gpkInfo) << LOG_KV("paramInfo", paramInfo);
            _callParameters->setExecResult(codec->encode(u256((int)VERIFY_GROUP_SIG_FAILED)));
            getErrorCodeOut(_callParameters->mutableExecResult(), 1, *codec);
            return _callParameters;
        }
        _callParameters->setExecResult(codec->encode(result));
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("GroupSigPrecompiled")
                               << LOG_DESC("call undefined function") << LOG_KV("func", func);
        _callParameters->setExecResult(codec->encode(u256((int)CODE_UNKNOW_FUNCTION_CALL)));
        getErrorCodeOut(_callParameters->mutableExecResult(), 1, *codec);
    }
    gasPricer->updateMemUsed(_callParameters->m_execResult.size());
    _callParameters->setGas(gasPricer->calTotalGas());
    return _callParameters;
}