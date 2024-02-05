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
 *  @author xingqiangbai
 *  @date 20230710
 */

#include "PaillierPrecompiled.h"
#include "../../executive/BlockContext.h"
#include "bcos-executor/src/precompiled/common/Common.h"
#include "bcos-executor/src/precompiled/common/PrecompiledResult.h"
#include "bcos-executor/src/precompiled/common/Utilities.h"
#include <paillier/callpaillier.h>
#include <string>

using namespace bcos;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::precompiled;
#if 0
contract Paillier
{
    function paillierAdd(string cipher1, string cipher2) public constant returns(string);
}
#endif

const char* const PAILLIER_METHOD_SET_STR = "paillierAdd(string,string)";
const char* const PAILLIER_METHOD_ADD_RAW_STR = "paillierAdd(bytes,bytes)";

PaillierPrecompiled::PaillierPrecompiled(const crypto::Hash::Ptr& _hashImpl)
  : Precompiled(_hashImpl), m_callPaillier(std::make_shared<CallPaillier>())
{
    name2Selector[PAILLIER_METHOD_SET_STR] = getFuncSelector(PAILLIER_METHOD_SET_STR, _hashImpl);
    name2Selector[PAILLIER_METHOD_ADD_RAW_STR] =
        getFuncSelector(PAILLIER_METHOD_ADD_RAW_STR, _hashImpl);
}

PrecompiledExecResult::Ptr PaillierPrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive,
    PrecompiledExecResult::Ptr _callParameters)
{
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("PaillierPrecompiled") << LOG_DESC("call")
                           << LOG_KV("param", toHex(_callParameters->input()));

    // parse function name
    uint32_t func = getParamFunc(_callParameters->input());
    bytesConstRef data = _callParameters->params();
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();

    if (func == name2Selector[PAILLIER_METHOD_SET_STR])
    {  // paillierAdd(string,string)
        std::string cipher1;
        std::string cipher2;
        codec.decode(data, cipher1, cipher2);
        std::string result;
        try
        {
            result = m_callPaillier->paillierAdd(cipher1, cipher2);
            gasPricer->appendOperation(InterfaceOpcode::PaillierAdd);
            _callParameters->setExecResult(codec.encode(result));
        }
        catch (CallException& e)
        {
            PRECOMPILED_LOG(INFO) << LOG_BADGE("PaillierPrecompiled")
                                  << LOG_DESC(std::string(e.what())) << LOG_KV("cipher1", cipher1)
                                  << LOG_KV("cipher2", cipher2);
            getErrorCodeOut(_callParameters->mutableExecResult(), CODE_INVALID_CIPHERS, codec);
        }
    }
    else if (func == name2Selector[PAILLIER_METHOD_ADD_RAW_STR] &&
             blockContext.features().get(ledger::Features::Flag::feature_paillier_add_raw))
    {  // paillierAdd(bytes,bytes)
        bytes cipher1;
        bytes cipher2;
        codec.decode(data, cipher1, cipher2);
        bytes result;
        try
        {
            result = m_callPaillier->paillierAdd(cipher1, cipher2);
            gasPricer->appendOperation(InterfaceOpcode::PaillierAdd);
            _callParameters->setExecResult(codec.encode(result));
        }
        catch (CallException& e)
        {
            PRECOMPILED_LOG(INFO) << LOG_BADGE("PaillierPrecompiled")
                                  << LOG_DESC(std::string(e.what()))
                                  << LOG_KV("cipher1", toHex(cipher1))
                                  << LOG_KV("cipher2", toHex(cipher2));
            getErrorCodeOut(_callParameters->mutableExecResult(), CODE_INVALID_CIPHERS, codec);
        }
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("PaillierPrecompiled")
                               << LOG_DESC("call undefined function") << LOG_KV("func", func);
        _callParameters->setExecResult(codec.encode((int32_t)CODE_UNKNOW_FUNCTION_CALL, false));
    }
    gasPricer->updateMemUsed(_callParameters->m_execResult.size());
    _callParameters->setGasLeft(_callParameters->m_gasLeft - gasPricer->calTotalGas());
    return _callParameters;
}