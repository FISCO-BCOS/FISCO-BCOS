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
 * (c) 2016-2018 fisco-dev contributors.
 */
/** @file PaillierPrecompiled.h
 *  @author shawnhe
 *  @date 20190808
 */

#include "PaillierPrecompiled.h"
#include <libdevcore/Common.h>
#include <libethcore/ABI.h>
#include <paillier/callpaillier.h>
#include <string>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::precompiled;

const char* const PAILLIER_METHOD_SET_STR = "paillierAdd(string,string)";

PaillierPrecompiled::PaillierPrecompiled() : m_callPaillier(std::make_shared<CallPaillier>())
{
    name2Selector[PAILLIER_METHOD_SET_STR] = getFuncSelector(PAILLIER_METHOD_SET_STR);
}

bytes PaillierPrecompiled::call(ExecutiveContext::Ptr, bytesConstRef param, Address const&)
{
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("PaillierPrecompiled") << LOG_DESC("call")
                           << LOG_KV("param", toHex(param));

    // parse function name
    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    dev::eth::ContractABI abi;
    bytes out;

    if (func == name2Selector[PAILLIER_METHOD_SET_STR])
    {
        // paillierAdd(string,string)
        std::string cipher1, cipher2;
        abi.abiOut(data, cipher1, cipher2);
        std::string result;
        try
        {
            result = m_callPaillier->paillierAdd(cipher1, cipher2);
        }
        catch (CallException& e)
        {
            PRECOMPILED_LOG(ERROR)
                << LOG_BADGE("PaillierPrecompiled") << LOG_DESC(std::string(e.what()))
                << LOG_KV("cipher1", cipher1) << LOG_KV("cipher2", cipher2);
            getErrorCodeOut(out, CODE_INVALID_CIPHERS);
            return out;
        }
        out = abi.abiIn("", result);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("PaillierPrecompiled")
                               << LOG_DESC("call undefined function") << LOG_KV("func", func);
        getErrorCodeOut(out, CODE_UNKNOW_FUNCTION_CALL);
    }
    return out;
}
