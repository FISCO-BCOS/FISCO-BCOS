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
/** @file EntryPrecompiled.cpp
 *  @author ancelmo
 *  @date 20180921
 */
#include "EntryPrecompiled.h"
#include <libdevcore/easylog.h>
#include <libethcore/ABI.h>

using namespace dev;
using namespace dev::blockverifier;


std::string EntryPrecompiled::toString(std::shared_ptr<ExecutiveContext>)
{
    return "Entry";
}

bytes EntryPrecompiled::call(std::shared_ptr<ExecutiveContext> context, bytesConstRef param)
{
    LOG(DEBUG) << "call Entry:";


    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    LOG(DEBUG) << "func:" << std::hex << func;

    dev::eth::ContractABI abi;

    bytes out;

    switch (func)
    {
    case 0xfda69fae:
    {  // getInt(string)
        std::string str;
        abi.abiOut(data, str);

        std::string value = m_entry->getField(str);

        u256 num = boost::lexical_cast<u256>(value);
        out = abi.abiIn("", num);

        break;
    }
    case 0xa815ff15:
    {  // set(string,address)
        std::string str;
        Address address;
        abi.abiOut(data, str, address);

        auto stringPrecompiled = context->getPrecompiled(address);
        m_entry->setField(str, stringPrecompiled->toString(context));

        break;
    }
    case 0x2ef8ba74:
    {  // set(string,int256)
        std::string str;
        u256 value;
        abi.abiOut(data, str, value);

        m_entry->setField(str, boost::lexical_cast<std::string>(value));

        break;
    }
    case 0xe942b516:
    {  // set(string,string)
        std::string str;
        std::string value;
        abi.abiOut(data, str, value);

        m_entry->setField(str, value);

        break;
    }
    case 0xbf40fac1:
    {  // getAddress(string)
        std::string str;
        abi.abiOut(data, str);

        std::string value = m_entry->getField(str);
        Address ret = Address(value);
        out = abi.abiIn("", ret);
        break;
    }
    case 0xd52decd4:
    {  //"getBytes64(string)"
        std::string str;
        abi.abiOut(data, str);

        std::string value = m_entry->getField(str);
        string64 ret;
        for (unsigned i = 0; i < 64; ++i)
            ret[i] = i < value.size() ? value[i] : 0;

        out = abi.abiIn("", ret);
        break;
    }
    default:
    {
        break;
    }
    }

    return out;
}
