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
#include "../../executive/BlockContext.h"
#include "bcos-executor/src/precompiled/common/PrecompiledResult.h"
#include "bcos-executor/src/precompiled/common/Utilities.h"
#include <group_sig/algorithm/RingSig.h>

using namespace bcos;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::precompiled;

/*
contract RingSig
{
    function ringSigVerify(string signature, string message, string paramInfo) public constant
returns(int, bool);
}
*/

const char* const RING_SIG_METHOD_SET_STR = "ringSigVerify(string,string,string)";

RingSigPrecompiled::RingSigPrecompiled(crypto::Hash::Ptr _hashImpl) : Precompiled(_hashImpl)
{
    name2Selector[RING_SIG_METHOD_SET_STR] = getFuncSelector(RING_SIG_METHOD_SET_STR, _hashImpl);
}

std::shared_ptr<PrecompiledExecResult> RingSigPrecompiled::call(
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

    if (func == name2Selector[RING_SIG_METHOD_SET_STR])
    {
        // ringSigVerify(string,string,string)
        std::string signature, message, paramInfo;
        codec->decode(data, signature, message, paramInfo);
        bool result = false;
        try
        {
            result = RingSigApi::LinkableRingSig::ring_verify(signature, message, paramInfo);
            gasPricer->appendOperation(InterfaceOpcode::GroupSigVerify);
        }
        catch (std::exception& error)
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("RingSigPrecompiled") << LOG_DESC(error.what())
                                   << LOG_KV("signature", signature) << LOG_KV("message", message)
                                   << LOG_KV("paramInfo", paramInfo);
            result = false;
        }
        catch (std::string& error)
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("RingSigPrecompiled") << LOG_DESC(error)
                                   << LOG_KV("signature", signature) << LOG_KV("message", message)
                                   << LOG_KV("paramInfo", paramInfo);
            result = false;
        }
        int32_t retCode = CODE_SUCCESS;
        if (!result)
        {
            retCode = VERIFY_RING_SIG_FAILED;
        }
        _callParameters->setExecResult(codec->encode(retCode, result));
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("RingSigPrecompiled")
                               << LOG_DESC("call undefined function") << LOG_KV("func", func);
        _callParameters->setExecResult(codec->encode((int32_t)CODE_UNKNOW_FUNCTION_CALL, false));
    }
    gasPricer->updateMemUsed(_callParameters->m_execResult.size());
    _callParameters->setGas(_callParameters->m_gas - gasPricer->calTotalGas());
    return _callParameters;
}
