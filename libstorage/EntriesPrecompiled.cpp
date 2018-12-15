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
#include "EntriesPrecompiled.h"
#include "EntryPrecompiled.h"
#include <libdevcore/easylog.h>
#include <libethcore/ABI.h>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::storage;

const char* const ENTRYIES_METHOD_get_int256 = " get(int256)";
const char* const ENTRYIES_METHOD_size = "size()";

EntriesPrecompiled::EntriesPrecompiled()
{
    name2Selector[ENTRYIES_METHOD_get_int256] =
        *(uint32_t*)(sha3(ENTRYIES_METHOD_get_int256).ref().cropped(0, 4).data());
    name2Selector[ENTRYIES_METHOD_size] =
        *(uint32_t*)(sha3(ENTRYIES_METHOD_size).ref().cropped(0, 4).data());
}

std::string dev::blockverifier::EntriesPrecompiled::toString(std::shared_ptr<ExecutiveContext>)
{
    return "Entries";
}

bytes dev::blockverifier::EntriesPrecompiled::call(
    std::shared_ptr<ExecutiveContext> context, bytesConstRef param)
{
    STORAGE_LOG(DEBUG) << "call Entries:" << toHex(param);

    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    STORAGE_LOG(DEBUG) << "func:" << std::hex << func;

    dev::eth::ContractABI abi;

    bytes out;

    if (func == name2Selector[ENTRYIES_METHOD_get_int256])
    {  // get(int256)
        u256 num;
        abi.abiOut(data, num);

        auto entry = m_entries->get(num.convert_to<size_t>());
        EntryPrecompiled::Ptr entryPrecompiled = std::make_shared<EntryPrecompiled>();
        entryPrecompiled->setEntry(entry);
        Address address = context->registerPrecompiled(entryPrecompiled);

        out = abi.abiIn("", address);
    }
    else if (func == name2Selector[ENTRYIES_METHOD_size])
    {  // size()
        u256 c = m_entries->size();

        out = abi.abiIn("", c);
    }
    return out;
}
