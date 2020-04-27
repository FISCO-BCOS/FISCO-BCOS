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
#include "libstorage/Table.h"
#include <libblockverifier/ExecutiveContext.h>
#include <libdevcrypto/Hash.h>
#include <libethcore/ABI.h>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::precompiled;
using namespace dev::storage;

const char* const ENTRY_GET_INT = "getInt(string)";
const char* const ENTRY_GET_UINT = "getUInt(string)";
const char* const ENTRY_SET_STR_INT = "set(string,int256)";
const char* const ENTRY_SET_STR_UINT = "set(string,uint256)";
const char* const ENTRY_SET_STR_ADDR = "set(string,address)";
const char* const ENTRY_SET_STR_STR = "set(string,string)";
const char* const ENTRY_GETA_STR = "getAddress(string)";
const char* const ENTRY_GETB_STR = "getBytes64(string)";
const char* const ENTRY_GETB_STR32 = "getBytes32(string)";
const char* const ENTRY_GET_STR = "getString(string)";

std::string setInt(bytesConstRef _data, std::string& _key, bool _isUint = false)
{
    dev::eth::ContractABI abi;
    std::string value;

    if (_isUint || g_BCOSConfig.version() <= RC2_VERSION)
    {
        u256 num;
        abi.abiOut(_data, _key, num);
        value = boost::lexical_cast<std::string>(num);
    }
    else
    {
        s256 num;
        abi.abiOut(_data, _key, num);
        value = boost::lexical_cast<std::string>(num);
    }
    return value;
}

EntryPrecompiled::EntryPrecompiled()
{
    name2Selector[ENTRY_GET_INT] = getFuncSelector(ENTRY_GET_INT);
    name2Selector[ENTRY_GET_UINT] = getFuncSelector(ENTRY_GET_UINT);
    name2Selector[ENTRY_SET_STR_INT] = getFuncSelector(ENTRY_SET_STR_INT);
    name2Selector[ENTRY_SET_STR_UINT] = getFuncSelector(ENTRY_SET_STR_UINT);
    name2Selector[ENTRY_SET_STR_STR] = getFuncSelector(ENTRY_SET_STR_STR);
    name2Selector[ENTRY_SET_STR_ADDR] = getFuncSelector(ENTRY_SET_STR_ADDR);
    name2Selector[ENTRY_GETA_STR] = getFuncSelector(ENTRY_GETA_STR);
    name2Selector[ENTRY_GETB_STR] = getFuncSelector(ENTRY_GETB_STR);
    name2Selector[ENTRY_GETB_STR32] = getFuncSelector(ENTRY_GETB_STR32);
    name2Selector[ENTRY_GET_STR] = getFuncSelector(ENTRY_GET_STR);
}

std::string EntryPrecompiled::toString()
{
    return "Entry";
}

PrecompiledExecResult::Ptr EntryPrecompiled::call(
    std::shared_ptr<ExecutiveContext>, bytesConstRef param, Address const&, Address const&)
{
    STORAGE_LOG(TRACE) << LOG_BADGE("EntryPrecompiled") << LOG_DESC("call")
                       << LOG_KV("param", toHex(param));

    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    dev::eth::ContractABI abi;
    auto callResult = m_precompiledExecResultFactory->createPrecompiledResult();
    callResult->gasPricer()->setMemUsed(param.size());

    if (func == name2Selector[ENTRY_GET_INT])
    {  // getInt(string)
        std::string str;
        abi.abiOut(data, str);
        s256 num = boost::lexical_cast<s256>(m_entry->getField(str));
        callResult->gasPricer()->appendOperation(InterfaceOpcode::GetInt);
        callResult->setExecResult(abi.abiIn("", num));
    }
    else if (func == name2Selector[ENTRY_GET_UINT])
    {  // getUInt(string)
        std::string str;
        abi.abiOut(data, str);
        u256 num = boost::lexical_cast<u256>(m_entry->getField(str));
        callResult->gasPricer()->appendOperation(InterfaceOpcode::GetInt);
        callResult->setExecResult(abi.abiIn("", num));
    }
    else if (func == name2Selector[ENTRY_SET_STR_INT])
    {  // set(string,int256)
        std::string key;
        std::string value(setInt(data, key));
        m_entry->setField(key, value);
        callResult->gasPricer()->appendOperation(InterfaceOpcode::Set);
    }
    else if (func == name2Selector[ENTRY_SET_STR_UINT])
    {  // set(string,uint256)
        std::string key;
        std::string value(setInt(data, key, true));
        m_entry->setField(key, value);
        callResult->gasPricer()->appendOperation(InterfaceOpcode::Set);
    }
    else if (func == name2Selector[ENTRY_SET_STR_STR])
    {  // set(string,string)
        std::string str;
        std::string value;
        abi.abiOut(data, str, value);

        m_entry->setField(str, value);
        callResult->gasPricer()->appendOperation(InterfaceOpcode::Set);
    }
    else if (func == name2Selector[ENTRY_SET_STR_ADDR])
    {  // set(string,address)
        std::string str;
        Address value;
        abi.abiOut(data, str, value);

        m_entry->setField(str, toHex(value));
        callResult->gasPricer()->appendOperation(InterfaceOpcode::Set);
    }
    else if (func == name2Selector[ENTRY_GETA_STR])
    {  // getAddress(string)
        std::string str;
        abi.abiOut(data, str);

        std::string value = m_entry->getField(str);
        Address ret = Address(value);
        callResult->setExecResult(abi.abiIn("", ret));
        callResult->gasPricer()->appendOperation(InterfaceOpcode::GetAddr);
    }
    else if (func == name2Selector[ENTRY_GETB_STR])
    {  // getBytes64(string)
        std::string str;
        abi.abiOut(data, str);

        std::string value = m_entry->getField(str);

        string32 ret0;
        string32 ret1;

        for (unsigned i = 0; i < 32; ++i)
            ret0[i] = (i < value.size() ? value[i] : 0);

        for (unsigned i = 32; i < 64; ++i)
            ret1[i - 32] = (i < value.size() ? value[i] : 0);
        callResult->setExecResult(abi.abiIn("", ret0, ret1));
        callResult->gasPricer()->appendOperation(InterfaceOpcode::GetByte64);
    }
    else if (func == name2Selector[ENTRY_GETB_STR32])
    {  // getBytes32(string)
        std::string str;
        abi.abiOut(data, str);

        std::string value = m_entry->getField(str);
        dev::string32 s32 = dev::eth::toString32(value);
        callResult->setExecResult(abi.abiIn("", s32));
        callResult->gasPricer()->appendOperation(InterfaceOpcode::GetByte32);
    }
    else if (func == name2Selector[ENTRY_GET_STR])
    {  // getString(string)
        std::string str;
        abi.abiOut(data, str);

        std::string value = m_entry->getField(str);
        callResult->setExecResult(abi.abiIn("", value));
        callResult->gasPricer()->appendOperation(InterfaceOpcode::GetString);
    }
    else
    {
        STORAGE_LOG(ERROR) << LOG_BADGE("EntryPrecompiled") << LOG_DESC("call undefined function!");
    }
    return callResult;
}
