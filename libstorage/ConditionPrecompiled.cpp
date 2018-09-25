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
#include <libdevcore/easylog.h>
#include <libethcore/ABI.h>

using namespace dev;
using namespace dev::blockverifier;


std::string ConditionPrecompiled::toString(std::shared_ptr<ExecutiveContext>)
{
    return "Condition";
}

bytes ConditionPrecompiled::call(std::shared_ptr<ExecutiveContext> context, bytesConstRef param)
{
    LOG(DEBUG) << "call Condition:" << toHex(param);

    // parse function name
    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    LOG(DEBUG) << "func:" << std::hex << func;

    dev::eth::ContractABI abi;

    bytes out;

    assert(m_condition);
    switch (func)
    {
    case 0xe44594b9:
    {  // EQ(string,int256)
        std::string str;
        u256 num;
        abi.abiOut(data, str, num);

        m_condition->EQ(str, boost::lexical_cast<std::string>(num));

        break;
    }
    case 0xcd30a1d1:
    {  // EQ(string,string)
        std::string str;
        std::string value;
        abi.abiOut(data, str, value);

        m_condition->EQ(str, value);

        break;
    }
    case 0x42f8dd31:
    {  // GE(string,int256)
        std::string str;
        u256 value;
        abi.abiOut(data, str, value);

        m_condition->GE(str, boost::lexical_cast<std::string>(value));

        break;
    }
    case 0x08ad6333:
    {  // GT(string,int256)
        std::string str;
        u256 value;
        abi.abiOut(data, str, value);

        m_condition->GT(str, boost::lexical_cast<std::string>(value));

        break;
    }
    case 0xb6f23857:
    {  // LE(string,int256)
        std::string str;
        u256 value;
        abi.abiOut(data, str, value);

        m_condition->LE(str, boost::lexical_cast<std::string>(value));

        break;
    }
    case 0xc31c9b65:
    {  // LT(string,int256)
        std::string str;
        u256 value;
        abi.abiOut(data, str, value);

        m_condition->LT(str, boost::lexical_cast<std::string>(value));

        break;
    }
    case 0x39aef024:
    {  // NE(string,int256)
        std::string str;
        u256 num;
        abi.abiOut(data, str, num);

        m_condition->NE(str, boost::lexical_cast<std::string>(num));

        break;
    }
    case 0x2783acf5:
    {  // NE(string,string)
        std::string str;
        std::string value;
        abi.abiOut(data, str, value);

        m_condition->NE(str, value);

        break;
    }
    case 0x2e0d738a:
    {  // limit(int256)
        u256 num;
        abi.abiOut(data, num);

        m_condition->limit(num.convert_to<size_t>());

        break;
    }
    case 0x7ec1cc65:
    {  // limit(int256,int256)
        u256 offset;
        u256 size;
        abi.abiOut(data, offset, size);

        m_condition->limit(offset.convert_to<size_t>(), size.convert_to<size_t>());

        break;
    }
    default:
    {
        break;
    }
    }

    return out;
}
