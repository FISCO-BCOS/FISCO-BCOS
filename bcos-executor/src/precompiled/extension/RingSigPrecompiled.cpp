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
 * @file RingSigPrecompiled.cpp
 * @author: yklu
 * @date 2022-04-12
 */

#include "RingSigPrecompiled.h"
#include <group_sig/algorithm/RingSig.h>
#include "../../executive/BlockContext.h"
#include "../PrecompiledResult.h"
#include "../Utilities.h"

using namespace bcos;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::precompiled;

/*
contract RingSig
{
    function ringSigVerify(string signature, string message, string paramInfo) public constant
returns(bool);
}
*/

const char* const RingSig_METHOD_SET_STR = "ringSigVerify(string,string,string)";

RingSigPrecompiled::RingSigPrecompiled(crypto::Hash::Ptr _hashImpl) : Precompiled(_hashImpl)
{
    name2Selector[RingSig_METHOD_SET_STR] = getFuncSelector(RingSig_METHOD_SET_STR, _hashImpl);
}

std::shared_ptr<PrecompiledExecResult> RingSigPrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef _param,
        const std::string& , const std::string& , int64_t)
{
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("RingSigPrecompiled") << LOG_DESC("call")
                           << LOG_KV("param", toHex(_param));

    // parse function name
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<CodecWrapper>(blockContext->hashHandler(), blockContext->isWasm());
    auto callResult = std::make_shared<PrecompiledExecResult>();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();
    gasPricer->setMemUsed(_param.size());

    if (func == name2Selector[RingSig_METHOD_SET_STR])
    {
        // ringSigVerify(string,string,string)
        std::string signature, message, paramInfo;
        codec->decode(data, signature, message, gpkInfo, paramInfo);
        bool result = false;

        try
        {
            result = RingSigApi::LinkableRingSig::ring_verify(signature, message, paramInfo);
            gasPricer->appendOperation(InterfaceOpcode::GroupSigVerify);
        }
        catch (std::string& errorMsg)
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("RingSigPrecompiled") << LOG_DESC(errorMsg)
                                   << LOG_KV("signature", signature) << LOG_KV("message", message)
                                   << LOG_KV("paramInfo", paramInfo);
            callResult->setExecResult(codec->encode(u256((int)VERIFY_GROUP_SIG_FAILED)));
            getErrorCodeOut(callResult->mutableExecResult(), 1, *codec);
            return callResult;
        }
        callResult->setExecResult(acodec->encode(result));
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("RingSigPrecompiled")
                               << LOG_DESC("call undefined function") << LOG_KV("func", func);
        callResult->setExecResult(codec->encode(u256((int)CODE_UNKNOW_FUNCTION_CALL)));
        getErrorCodeOut(callResult->mutableExecResult(), 1, *codec);
    }
    gasPricer->updateMemUsed(callResult->m_execResult.size());
    callResult->setGas(gasPricer->calTotalGas());
    return callResult;
}
