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
#include "EntriesPrecompiled.h"
#include "EntryPrecompiled.h"
#include <libethcore/ABI.h>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::precompiled;
using namespace dev::storage;

const char* const ENTRIES_GET_INT = "get(int256)";
const char* const ENTRIES_SIZE = "size()";

EntriesPrecompiled::EntriesPrecompiled()
{
    name2Selector[ENTRIES_GET_INT] = getFuncSelector(ENTRIES_GET_INT);
    name2Selector[ENTRIES_SIZE] = getFuncSelector(ENTRIES_SIZE);
}

std::string EntriesPrecompiled::toString()
{
    return "Entries";
}

bytes EntriesPrecompiled::call(
    ExecutiveContext::Ptr context, bytesConstRef param, Address const&, Address const&)
{
    STORAGE_LOG(TRACE) << LOG_BADGE("EntriesPrecompiled") << LOG_DESC("call")
                       << LOG_KV("param", toHex(param));
    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    dev::eth::ContractABI abi;

    bytes out;

    if (func == name2Selector[ENTRIES_GET_INT])
    {  // get(int256)
        u256 num;
        abi.abiOut(data, num);

        Entry::Ptr entry = getEntries()->get(num.convert_to<size_t>());
        EntryPrecompiled::Ptr entryPrecompiled = std::make_shared<EntryPrecompiled>();
        entryPrecompiled->setEntry(entry);
        Address address = context->registerPrecompiled(entryPrecompiled);

        out = abi.abiIn("", address);
    }
    else if (func == name2Selector[ENTRIES_SIZE])
    {  // size()
        u256 c = getEntries()->size();

        out = abi.abiIn("", c);
    }
    else
    {
        STORAGE_LOG(ERROR) << LOG_BADGE("EntriesPrecompiled")
                           << LOG_DESC("call undefined function!");
    }
    return out;
}
