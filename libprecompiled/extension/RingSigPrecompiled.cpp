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
#include <libdevcore/Common.h>
#include <libdevcore/easylog.h>
#include <libethcore/ABI.h>
#include <string>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::precompiled;

const char* const RingSig_METHOD_SET_STR = "ringSigVerify(string,string,string)";

RingSigPrecompiled::RingSigPrecompiled()
{
    name2Selector[RingSig_METHOD_SET_STR] = getFuncSelector(RingSig_METHOD_SET_STR);
}

bytes RingSigPrecompiled::call(ExecutiveContext::Ptr, bytesConstRef param, Address const&)
{
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("RingSigPrecompiled") << LOG_DESC("call")
                           << LOG_KV("param", toHex(param));

    // parse function name
    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    dev::eth::ContractABI abi;
    bytes out;

    if (func == name2Selector[RingSig_METHOD_SET_STR])
    {
        // ringSigVerify(string,string,string)
        std::string signature, message, paramInfo;
        abi.abiOut(data, signature, message, paramInfo);
        bool result = false;

        try
        {
            result = RingSigApi::LinkableRingSig::ring_verify(signature, message, paramInfo);
        }
        catch (std::string& errorMsg)
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("RingSigPrecompiled") << LOG_DESC(errorMsg)
                                   << LOG_KV("signature", signature) << LOG_KV("message", message)
                                   << LOG_KV("paramInfo", paramInfo);
            getErrorCodeOut(out, VERIFY_RING_SIG_FAILED);
            return out;
        }
        out = abi.abiIn("", result);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("RingSigPrecompiled")
                               << LOG_DESC("call undefined function") << LOG_KV("func", func);
        getErrorCodeOut(out, CODE_UNKNOW_FUNCTION_CALL);
    }
    return out;
}
