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
 * @file RandomPrecompiled.cpp
 * @author: yujiechen
 * @date 2022-04-06
 */
#include "RandomPrecompiled.h"
#include "PrecompiledResult.h"
#include "Utilities.h"
#include <bcos-codec/abi/ContractABICodec.h>
using namespace bcos;
using namespace bcos::precompiled;

const char* const GENERATE_RANDOM_VALUE_METHOD_STR = "generateRandomValue()";

RandomPrecompiled::RandomPrecompiled(bcos::crypto::Hash::Ptr _hashImpl) : Precompiled(_hashImpl)
{
    name2Selector[GENERATE_RANDOM_VALUE_METHOD_STR] =
        getFuncSelector(GENERATE_RANDOM_VALUE_METHOD_STR, _hashImpl);
}


std::shared_ptr<PrecompiledExecResult> RandomPrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef _param,
    const std::string&, const std::string&)
{
    auto func = getParamFunc(_param);
    auto callResult = std::make_shared<PrecompiledExecResult>();
    if (func == name2Selector[GENERATE_RANDOM_VALUE_METHOD_STR])
    {
        auto blockContext = _executive->blockContext().lock();
        auto codec =
            std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
        srand((unsigned)utcTime());
        auto result = rand();
        getErrorCodeOut(callResult->mutableExecResult(), result, *codec);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("RandomPrecompiled")
                               << LOG_DESC("call undefined function") << LOG_KV("func", func);
    }
    return callResult;
}
