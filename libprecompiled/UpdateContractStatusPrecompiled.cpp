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
/** @file UpdateContractStatusPrecompiled.cpp
 *  @author chaychen
 *  @date 20190106
 */
#include "UpdateContractStatusPrecompiled.h"

#include "libprecompiled/EntriesPrecompiled.h"
#include "libprecompiled/TableFactoryPrecompiled.h"
#include "libstoragestate/StorageState.h"
#include <libethcore/ABI.h>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::storage;
using namespace dev::precompiled;

// precompiled contract function
const char* const METHOD_FROZEN_STR = "setFrozenStatus(address,bool)";
const char* const METHOD_KILL_STR = "kill(address)";
const char* const METHOD_QUERY_STR = "query(address)";

// contract state
// available,frozen,killed

// state-transition matrix
/*
                                available frozen    killed
   setFrozenStatus(addr,true)   frozen    frozen    ×
   setFrozenStatus(addr,false)  available available ×
   kill(addr)                   killed    killed    killed
*/

UpdateContractStatusPrecompiled::UpdateContractStatusPrecompiled()
{
    name2Selector[METHOD_FROZEN_STR] = getFuncSelector(METHOD_FROZEN_STR);
    name2Selector[METHOD_KILL_STR] = getFuncSelector(METHOD_KILL_STR);
    name2Selector[METHOD_QUERY_STR] = getFuncSelector(METHOD_QUERY_STR);
}

bool UpdateContractStatusPrecompiled::checkAddress(Address const& contractAddress)
{
    bool isValidAddress = true;
    try
    {
        Address address(contractAddress);
        (void)address;
    }
    catch (...)
    {
        isValidAddress = false;
    }
    return isValidAddress;
}

bytes UpdateContractStatusPrecompiled::call(
    ExecutiveContext::Ptr context, bytesConstRef param, Address const& origin)
{
    (void)context;
    (void)origin;
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("UpdateContractStatusPrecompiled")
                           << LOG_KV("call param", toHex(param));

    // parse function name
    uint32_t func = getParamFunc(param);
    bytes out;

    if (func == name2Selector[METHOD_KILL_STR])
    {
    }
    else if (func == name2Selector[METHOD_FROZEN_STR])
    {
    }
    else if (func == name2Selector[METHOD_QUERY_STR])
    {
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("UpdateContractStatusPrecompiled")
                               << LOG_DESC("call undefined function") << LOG_KV("func", func);
    }

    return out;
}
