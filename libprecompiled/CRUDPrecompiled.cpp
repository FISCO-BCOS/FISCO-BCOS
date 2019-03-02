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
/** @file CRUDPrecompiled.cpp
 *  @author ancelmo
 *  @date 20180921
 */

#include "CRUDPrecompiled.h"

#include "libstorage/EntriesPrecompiled.h"
#include "libstorage/TableFactoryPrecompiled.h"
#include <libdevcore/Common.h>
#include <libdevcore/easylog.h>
#include <libdevcrypto/Hash.h>
#include <libethcore/ABI.h>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::storage;
using namespace dev::precompiled;

const char* const CRUD_METHOD_SLT_STR_STR = "select(string,string)";

CRUDPrecompiled::CRUDPrecompiled()
{
    name2Selector[CRUD_METHOD_SLT_STR_STR] = getFuncSelector(CRUD_METHOD_SLT_STR_STR);
}

std::string CRUDPrecompiled::toString()
{
    return "CRUD";
}

bytes CRUDPrecompiled::call(ExecutiveContext::Ptr context, bytesConstRef param, Address const&)
{
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("CRUDPrecompiled") << LOG_DESC("call")
                           << LOG_KV("param", toHex(param));
    // parse function name
    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    dev::eth::ContractABI abi;
    bytes out;

    if (func == name2Selector[CRUD_METHOD_SLT_STR_STR])
    {  // select(string,string)
        std::string tableName, key;
        abi.abiOut(data, tableName, key);
        storage::Table::Ptr table = openTable(context, tableName);
        if (table.get())
        {
            auto entries = table->select(key, table->newCondition());
            auto entriesPrecompiled = std::make_shared<EntriesPrecompiled>();
            entriesPrecompiled->setEntries(entries);
            auto newAddress = context->registerPrecompiled(entriesPrecompiled);
            out = abi.abiIn("", newAddress);
        }
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("CRUDPrecompiled")
                               << LOG_DESC("call undefined function") << LOG_KV("func", func);
    }
    return out;
}
