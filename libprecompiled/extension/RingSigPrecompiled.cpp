/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2019 fisco-dev contributors.
 */
/** @file RingSigPrecompiled.cpp
 *  @author shawnhe
 *  @date 20190821
 */

#include "RingSigPrecompiled.h"
#include <group_sig/algorithm/RingSig.h>
#include <libprotocol/ABI.h>
#include <libutilities/Common.h>
#include <string>

using namespace bcos;
using namespace bcos::blockverifier;
using namespace bcos::precompiled;

const char* const RingSig_METHOD_SET_STR = "ringSigVerify(string,string,string)";

RingSigPrecompiled::RingSigPrecompiled()
{
    name2Selector[RingSig_METHOD_SET_STR] = getFuncSelector(RingSig_METHOD_SET_STR);
}

PrecompiledExecResult::Ptr RingSigPrecompiled::call(
    ExecutiveContext::Ptr, bytesConstRef param, Address const&, Address const&)
{
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("RingSigPrecompiled") << LOG_DESC("call")
                           << LOG_KV("param", *toHexString(param));

    // parse function name
    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    bcos::protocol::ContractABI abi;
    auto callResult = m_precompiledExecResultFactory->createPrecompiledResult();
    callResult->gasPricer()->setMemUsed(param.size());

    if (func == name2Selector[RingSig_METHOD_SET_STR])
    {
        // ringSigVerify(string,string,string)
        std::string signature, message, paramInfo;
        abi.abiOut(data, signature, message, paramInfo);
        bool result = false;

        try
        {
            result = RingSigApi::LinkableRingSig::ring_verify(signature, message, paramInfo);
            callResult->gasPricer()->appendOperation(InterfaceOpcode::RingSigVerify);
        }
        catch (std::string& errorMsg)
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("RingSigPrecompiled") << LOG_DESC(errorMsg)
                                   << LOG_KV("signature", signature) << LOG_KV("message", message)
                                   << LOG_KV("paramInfo", paramInfo);
            getErrorCodeOut(callResult->mutableExecResult(), VERIFY_RING_SIG_FAILED);
            return callResult;
        }
        callResult->setExecResult(abi.abiIn("", result));
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("RingSigPrecompiled")
                               << LOG_DESC("call undefined function") << LOG_KV("func", func);
        getErrorCodeOut(callResult->mutableExecResult(), CODE_UNKNOW_FUNCTION_CALL);
    }
    return callResult;
}
