/*
    This file is part of FISCO-BCOS.

    FISCO-BCOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FISCO-BCOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file ConditionPrecompiled.h
 *  @author ancelmo
 *  @date 20180921
 */
#include "ConditionPrecompiled.h"
#include "Common.h"
#include <libdevcore/easylog.h>
#include <libdevcrypto/Hash.h>
#include <libethcore/ABI.h>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::storage;


const char* const CONDITION_METHOD_EQ_string_int256 = "EQ(string,int256)";
const char* const CONDITION_METHOD_EQ_string_string = "EQ(string,string)";
const char* const CONDITION_METHOD_GE_string_int256 = "GE(string,int256)";
const char* const CONDITION_METHOD_GT_string_int256 = "GT(string,int256)";
const char* const CONDITION_METHOD_LE_string_int256 = "LE(string,int256)";
const char* const CONDITION_METHOD_LT_string_int256 = "LT(string,int256)";
const char* const CONDITION_METHOD_NE_string_int256 = "NE(string,int256)";
const char* const CONDITION_METHOD_NE_string_string = "NE(string,string)";
const char* const CONDITION_METHOD_limit_int256 = "limit(int256)";
const char* const CONDITION_METHOD_limit_int256_int256 = "limit(int256,int256)";

ConditionPrecompiled::ConditionPrecompiled()
{
    name2Selector[CONDITION_METHOD_EQ_string_int256] =
        *(uint32_t*)(sha3(CONDITION_METHOD_EQ_string_int256).ref().cropped(0, 4).data());
    name2Selector[CONDITION_METHOD_EQ_string_string] =
        *(uint32_t*)(sha3(CONDITION_METHOD_EQ_string_string).ref().cropped(0, 4).data());
    name2Selector[CONDITION_METHOD_GE_string_int256] =
        *(uint32_t*)(sha3(CONDITION_METHOD_GE_string_int256).ref().cropped(0, 4).data());
    name2Selector[CONDITION_METHOD_GT_string_int256] =
        *(uint32_t*)(sha3(CONDITION_METHOD_GT_string_int256).ref().cropped(0, 4).data());
    name2Selector[CONDITION_METHOD_LE_string_int256] =
        *(uint32_t*)(sha3(CONDITION_METHOD_LE_string_int256).ref().cropped(0, 4).data());
    name2Selector[CONDITION_METHOD_LT_string_int256] =
        *(uint32_t*)(sha3(CONDITION_METHOD_LT_string_int256).ref().cropped(0, 4).data());
    name2Selector[CONDITION_METHOD_NE_string_int256] =
        *(uint32_t*)(sha3(CONDITION_METHOD_NE_string_int256).ref().cropped(0, 4).data());
    name2Selector[CONDITION_METHOD_NE_string_string] =
        *(uint32_t*)(sha3(CONDITION_METHOD_NE_string_string).ref().cropped(0, 4).data());
    name2Selector[CONDITION_METHOD_limit_int256] =
        *(uint32_t*)(sha3(CONDITION_METHOD_limit_int256).ref().cropped(0, 4).data());
    name2Selector[CONDITION_METHOD_limit_int256_int256] =
        *(uint32_t*)(sha3(CONDITION_METHOD_limit_int256_int256).ref().cropped(0, 4).data());
}

std::string ConditionPrecompiled::toString(std::shared_ptr<ExecutiveContext>)
{
    return "Condition";
}

bytes ConditionPrecompiled::call(std::shared_ptr<ExecutiveContext> context, bytesConstRef param)
{
    STORAGE_LOG(DEBUG) << "call Condition:" << toHex(param);

    // parse function name
    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    STORAGE_LOG(DEBUG) << "func:" << std::hex << func;

    dev::eth::ContractABI abi;

    bytes out;

    assert(m_condition);
    if (func == name2Selector[CONDITION_METHOD_EQ_string_int256])
    {
        // EQ(string,int256)
        std::string str;
        u256 num;
        abi.abiOut(data, str, num);

        m_condition->EQ(str, boost::lexical_cast<std::string>(num));
    }
    else if (func == name2Selector[CONDITION_METHOD_EQ_string_string])
    {  // EQ(string,string)
        std::string str;
        std::string value;
        abi.abiOut(data, str, value);

        m_condition->EQ(str, value);
    }
    else if (func == name2Selector[CONDITION_METHOD_GE_string_int256])
    {  // GE(string,int256)
        std::string str;
        u256 value;
        abi.abiOut(data, str, value);

        m_condition->GE(str, boost::lexical_cast<std::string>(value));
    }
    else if (func == name2Selector[CONDITION_METHOD_GT_string_int256])
    {  // GT(string,int256)
        std::string str;
        u256 value;
        abi.abiOut(data, str, value);

        m_condition->GT(str, boost::lexical_cast<std::string>(value));
    }
    else if (func == name2Selector[CONDITION_METHOD_LE_string_int256])
    {  // LE(string,int256)
        std::string str;
        u256 value;
        abi.abiOut(data, str, value);

        m_condition->LE(str, boost::lexical_cast<std::string>(value));
    }
    else if (func == name2Selector[CONDITION_METHOD_LT_string_int256])
    {  // LT(string,int256)
        std::string str;
        u256 value;
        abi.abiOut(data, str, value);

        m_condition->LT(str, boost::lexical_cast<std::string>(value));
    }
    else if (func == name2Selector[CONDITION_METHOD_NE_string_int256])
    {  // NE(string,int256)
        std::string str;
        u256 num;
        abi.abiOut(data, str, num);

        m_condition->NE(str, boost::lexical_cast<std::string>(num));
    }
    else if (func == name2Selector[CONDITION_METHOD_NE_string_string])
    {  // NE(string,string)
        std::string str;
        std::string value;
        abi.abiOut(data, str, value);

        m_condition->NE(str, value);
    }
    else if (func == name2Selector[CONDITION_METHOD_limit_int256])
    {  // limit(int256)
        u256 num;
        abi.abiOut(data, num);

        m_condition->limit(num.convert_to<size_t>());
    }
    else if (func == name2Selector[CONDITION_METHOD_limit_int256_int256])
    {  // limit(int256,int256)
        u256 offset;
        u256 size;
        abi.abiOut(data, offset, size);

        m_condition->limit(offset.convert_to<size_t>(), size.convert_to<size_t>());
    }
    return out;
}