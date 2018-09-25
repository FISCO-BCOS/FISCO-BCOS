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


std::string dev::blockverifier::EntriesPrecompiled::toString(std::shared_ptr<ExecutiveContext>)
{
    return "Entries";
}

bytes dev::blockverifier::EntriesPrecompiled::call(
    std::shared_ptr<ExecutiveContext> context, bytesConstRef param)
{
    LOG(DEBUG) << "call Entries:" << toHex(param);

    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    LOG(DEBUG) << "func:" << std::hex << func;

    dev::eth::ContractABI abi;

    bytes out;

    switch (func)
    {
    case 0x846719e0:
    {  // get(int256)
        u256 num;
        abi.abiOut(data, num);

        auto entry = m_entries->get(num.convert_to<size_t>());
        EntryPrecompiled::Ptr entryPrecompiled = std::make_shared<EntryPrecompiled>();
        entryPrecompiled->setEntry(entry);
        Address address = context->registerPrecompiled(entryPrecompiled);

        out = abi.abiIn("", address);

        break;
    }
    case 0x949d225d:
    {  // size()
        u256 c = m_entries->size();

        out = abi.abiIn("", c);

        break;
    }
    default:
    {
        break;
    }
    }

    return out;
}
