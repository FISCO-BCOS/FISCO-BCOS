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
/** @file ConditionPrecompiled.h
 *  @author ancelmo
 *  @date 20180921
 */
#include "ConditionPrecompiled.h"
#include "libstorage/Common.h"
#include <libdevcrypto/Hash.h>
#include <libethcore/ABI.h>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::precompiled;
using namespace dev::storage;


const char* const CONDITION_METHOD_EQ_STR_INT = "EQ(string,int256)";
const char* const CONDITION_METHOD_EQ_STR_STR = "EQ(string,string)";
const char* const CONDITION_METHOD_GE_STR_INT = "GE(string,int256)";
const char* const CONDITION_METHOD_GT_STR_INT = "GT(string,int256)";
const char* const CONDITION_METHOD_LE_STR_INT = "LE(string,int256)";
const char* const CONDITION_METHOD_LT_STR_INT = "LT(string,int256)";
const char* const CONDITION_METHOD_NE_STR_INT = "NE(string,int256)";
const char* const CONDITION_METHOD_NE_STR_STR = "NE(string,string)";
const char* const CONDITION_METHOD_LIMIT_INT = "limit(int256)";
const char* const CONDITION_METHOD_LIMIT_2INT = "limit(int256,int256)";

ConditionPrecompiled::ConditionPrecompiled()
{
    name2Selector[CONDITION_METHOD_EQ_STR_INT] = getFuncSelector(CONDITION_METHOD_EQ_STR_INT);
    name2Selector[CONDITION_METHOD_EQ_STR_STR] = getFuncSelector(CONDITION_METHOD_EQ_STR_STR);
    name2Selector[CONDITION_METHOD_GE_STR_INT] = getFuncSelector(CONDITION_METHOD_GE_STR_INT);
    name2Selector[CONDITION_METHOD_GT_STR_INT] = getFuncSelector(CONDITION_METHOD_GT_STR_INT);
    name2Selector[CONDITION_METHOD_LE_STR_INT] = getFuncSelector(CONDITION_METHOD_LE_STR_INT);
    name2Selector[CONDITION_METHOD_LT_STR_INT] = getFuncSelector(CONDITION_METHOD_LT_STR_INT);
    name2Selector[CONDITION_METHOD_NE_STR_INT] = getFuncSelector(CONDITION_METHOD_NE_STR_INT);
    name2Selector[CONDITION_METHOD_NE_STR_STR] = getFuncSelector(CONDITION_METHOD_NE_STR_STR);
    name2Selector[CONDITION_METHOD_LIMIT_INT] = getFuncSelector(CONDITION_METHOD_LIMIT_INT);
    name2Selector[CONDITION_METHOD_LIMIT_2INT] = getFuncSelector(CONDITION_METHOD_LIMIT_2INT);
}

std::string ConditionPrecompiled::toString()
{
    return "Condition";
}

PrecompiledExecResult::Ptr ConditionPrecompiled::call(
    ExecutiveContext::Ptr, bytesConstRef param, Address const&, Address const&)
{
    // parse function name
    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    STORAGE_LOG(DEBUG) << "func:" << std::hex << func;

    dev::eth::ContractABI abi;

    auto callResult = m_precompiledExecResultFactory->createPrecompiledResult();
    callResult->gasPricer()->setMemUsed(param.size());
    // ensured by the logic of code
    assert(m_condition);
    if (func == name2Selector[CONDITION_METHOD_EQ_STR_INT])
    {
        // EQ(string,int256)
        std::string str;
        s256 num;
        abi.abiOut(data, str, num);

        m_condition->EQ(str, boost::lexical_cast<std::string>(num));
        callResult->gasPricer()->appendOperation(InterfaceOpcode::EQ);
    }
    else if (func == name2Selector[CONDITION_METHOD_EQ_STR_STR])
    {  // EQ(string,string)
        std::string str;
        std::string value;
        abi.abiOut(data, str, value);

        m_condition->EQ(str, value);
        callResult->gasPricer()->appendOperation(InterfaceOpcode::EQ);
    }
    else if (func == name2Selector[CONDITION_METHOD_GE_STR_INT])
    {  // GE(string,int256)
        std::string str;
        s256 value;
        abi.abiOut(data, str, value);

        m_condition->GE(str, boost::lexical_cast<std::string>(value));
        callResult->gasPricer()->appendOperation(InterfaceOpcode::GE);
    }
    else if (func == name2Selector[CONDITION_METHOD_GT_STR_INT])
    {  // GT(string,int256)
        std::string str;
        s256 value;
        abi.abiOut(data, str, value);

        m_condition->GT(str, boost::lexical_cast<std::string>(value));
        callResult->gasPricer()->appendOperation(InterfaceOpcode::GT);
    }
    else if (func == name2Selector[CONDITION_METHOD_LE_STR_INT])
    {  // LE(string,int256)
        std::string str;
        s256 value;
        abi.abiOut(data, str, value);

        m_condition->LE(str, boost::lexical_cast<std::string>(value));
        callResult->gasPricer()->appendOperation(InterfaceOpcode::LE);
    }
    else if (func == name2Selector[CONDITION_METHOD_LT_STR_INT])
    {  // LT(string,int256)
        std::string str;
        s256 value;
        abi.abiOut(data, str, value);

        m_condition->LT(str, boost::lexical_cast<std::string>(value));
        callResult->gasPricer()->appendOperation(InterfaceOpcode::LT);
    }
    else if (func == name2Selector[CONDITION_METHOD_NE_STR_INT])
    {  // NE(string,int256)
        std::string str;
        s256 num;
        abi.abiOut(data, str, num);

        m_condition->NE(str, boost::lexical_cast<std::string>(num));
        callResult->gasPricer()->appendOperation(InterfaceOpcode::NE);
    }
    else if (func == name2Selector[CONDITION_METHOD_NE_STR_STR])
    {  // NE(string,string)
        std::string str;
        std::string value;
        abi.abiOut(data, str, value);

        m_condition->NE(str, value);
        callResult->gasPricer()->appendOperation(InterfaceOpcode::NE);
    }
    else if (func == name2Selector[CONDITION_METHOD_LIMIT_INT])
    {  // limit(int256)
        s256 num;
        abi.abiOut(data, num);

        m_condition->limit(num.convert_to<size_t>());
        callResult->gasPricer()->appendOperation(InterfaceOpcode::Limit);
    }
    else if (func == name2Selector[CONDITION_METHOD_LIMIT_2INT])
    {  // limit(int256,int256)
        s256 offset;
        s256 size;
        abi.abiOut(data, offset, size);

        m_condition->limit(offset.convert_to<size_t>(), size.convert_to<size_t>());
        callResult->gasPricer()->appendOperation(InterfaceOpcode::Limit);
    }
    else
    {
        STORAGE_LOG(ERROR) << LOG_BADGE("ConditionPrecompiled")
                           << LOG_DESC("call undefined function!");
    }
    return callResult;
}