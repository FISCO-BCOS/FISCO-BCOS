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
/** @file EntryPrecompiled.cpp
 *  @author ancelmo
 *  @date 20180921
 */
#include "EntryPrecompiled.h"
#include "Table.h"
#include <libblockverifier/ExecutiveContext.h>
#include <libdevcore/easylog.h>
#include <libdevcrypto/Hash.h>
#include <libethcore/ABI.h>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::storage;

const char* const ENTRYIES_METENTRYIY_METHOD_GETI_STR = "getInt(string)";
const char* const ENTRYIY_METHOD_SET_STR_INT = "set(string,int256)";
const char* const ENTRYIY_METHOD_SET_STR_STR = "set(string,string)";
const char* const ENTRYIY_METHOD_GETA_STR = "getAddress(string)";
const char* const ENTRYIY_METHOD_GETB_STR = "getBytes64(string)";
const char* const ENTRYIY_METHOD_GETB_STR32 = "getBytes32(string)";

EntryPrecompiled::EntryPrecompiled()
{
    name2Selector[ENTRYIES_METENTRYIY_METHOD_GETI_STR] =
        getFuncSelector(ENTRYIES_METENTRYIY_METHOD_GETI_STR);
    name2Selector[ENTRYIY_METHOD_SET_STR_INT] = getFuncSelector(ENTRYIY_METHOD_SET_STR_INT);
    name2Selector[ENTRYIY_METHOD_SET_STR_STR] = getFuncSelector(ENTRYIY_METHOD_SET_STR_STR);
    name2Selector[ENTRYIY_METHOD_GETA_STR] = getFuncSelector(ENTRYIY_METHOD_GETA_STR);
    name2Selector[ENTRYIY_METHOD_GETB_STR] = getFuncSelector(ENTRYIY_METHOD_GETB_STR);
    name2Selector[ENTRYIY_METHOD_GETB_STR32] = getFuncSelector(ENTRYIY_METHOD_GETB_STR32);
}

std::string EntryPrecompiled::toString()
{
    return "Entry";
}

bytes EntryPrecompiled::call(std::shared_ptr<ExecutiveContext>, bytesConstRef param, Address const&)
{
    STORAGE_LOG(TRACE) << LOG_BADGE("EntryPrecompiled") << LOG_DESC("call")
                       << LOG_KV("param", toHex(param));

    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    dev::eth::ContractABI abi;

    bytes out;

    if (func == name2Selector[ENTRYIES_METENTRYIY_METHOD_GETI_STR])
    {  // getInt(string)
        std::string str;
        abi.abiOut(data, str);

        std::string value = m_entry->getField(str);

        u256 num = boost::lexical_cast<u256>(value);
        out = abi.abiIn("", num);
    }
    else if (func == name2Selector[ENTRYIY_METHOD_SET_STR_INT])
    {  // set(string,int256)
        std::string str;
        u256 value;
        abi.abiOut(data, str, value);

        m_entry->setField(str, boost::lexical_cast<std::string>(value));
    }
    else if (func == name2Selector[ENTRYIY_METHOD_SET_STR_STR])
    {  // set(string,string)
        std::string str;
        std::string value;
        abi.abiOut(data, str, value);

        m_entry->setField(str, value);
    }
    else if (func == name2Selector[ENTRYIY_METHOD_GETA_STR])
    {  // getAddress(string)
        std::string str;
        abi.abiOut(data, str);

        std::string value = m_entry->getField(str);
        Address ret = Address(value);
        out = abi.abiIn("", ret);
    }
    else if (func == name2Selector[ENTRYIY_METHOD_GETB_STR])
    {  // getBytes64(string)
        std::string str;
        abi.abiOut(data, str);

        std::string value = m_entry->getField(str);
        string64 ret;
        for (unsigned i = 0; i < 64; ++i)
            ret[i] = i < value.size() ? value[i] : 0;

        out = abi.abiIn("", ret);
    }
    else if (func == name2Selector[ENTRYIY_METHOD_GETB_STR32])
    {  //"getBytes32(string)"
        std::string str;
        abi.abiOut(data, str);

        std::string value = m_entry->getField(str);
        dev::string32 s32 = dev::eth::toString32(value);
        out = abi.abiIn("", s32);
    }
    else
    {
        STORAGE_LOG(ERROR) << LOG_BADGE("EntryPrecompiled") << LOG_DESC("call undefined function!");
    }
    return out;
}
