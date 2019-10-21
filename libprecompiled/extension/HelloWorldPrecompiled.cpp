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
/** @file HelloWorldPrecompiled.cpp
 *  @author octopuswang
 *  @date 20190111
 */
#include "HelloWorldPrecompiled.h"
#include <libblockverifier/ExecutiveContext.h>
#include <libethcore/ABI.h>
#include <libprecompiled/TableFactoryPrecompiled.h>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::storage;
using namespace dev::precompiled;

/*
contract HelloWorld {
    function get() public constant returns(string);
    function set(string _m) public;
}
*/


// HelloWorldPrecompiled table name
const std::string HELLO_WORLD_TABLE_NAME = "hello_world";
// key field
const std::string HELLOWORLD_KEY_FIELD = "key";
const std::string HELLOWORLD_KEY_FIELD_NAME = "hello_key";
// value field
const std::string HELLOWORLD_VALUE_FIELD = "value";

// get interface
const char* const HELLO_WORLD_METHOD_GET = "get()";
// set interface
const char* const HELLO_WORLD_METHOD_SET = "set(string)";

HelloWorldPrecompiled::HelloWorldPrecompiled()
{
    name2Selector[HELLO_WORLD_METHOD_GET] = getFuncSelector(HELLO_WORLD_METHOD_GET);
    name2Selector[HELLO_WORLD_METHOD_SET] = getFuncSelector(HELLO_WORLD_METHOD_SET);
}

std::string HelloWorldPrecompiled::toString()
{
    return "HelloWorld";
}

bytes HelloWorldPrecompiled::call(dev::blockverifier::ExecutiveContext::Ptr _context,
    bytesConstRef _param, Address const& _origin)
{
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("HelloWorldPrecompiled") << LOG_DESC("call")
                           << LOG_KV("param", toHex(_param));

    // parse function name
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);
    bytes out;
    dev::eth::ContractABI abi;

    Table::Ptr table = openTable(_context, precompiled::getTableName(HELLO_WORLD_TABLE_NAME));
    if (!table)
    {
        // table is not exist, create it.
        table = createTable(_context, precompiled::getTableName(HELLO_WORLD_TABLE_NAME),
            HELLOWORLD_KEY_FIELD, HELLOWORLD_VALUE_FIELD, _origin);
        if (!table)
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("HelloWorldPrecompiled") << LOG_DESC("set")
                                   << LOG_DESC("open table failed.");
            getErrorCodeOut(out, storage::CODE_NO_AUTHORIZED);
            return out;
        }
    }
    if (func == name2Selector[HELLO_WORLD_METHOD_GET])
    {  // get() function call
        // default retMsg
        std::string retValue = "Hello World!";

        auto entries = table->select(HELLOWORLD_KEY_FIELD_NAME, table->newCondition());
        if (0u != entries->size())
        {
            auto entry = entries->get(0);
            retValue = entry->getField(HELLOWORLD_VALUE_FIELD);
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("HelloWorldPrecompiled") << LOG_DESC("get")
                                   << LOG_KV("value", retValue);
        }
        out = abi.abiIn("", retValue);
    }
    else if (func == name2Selector[HELLO_WORLD_METHOD_SET])
    {  // set(string) function call

        std::string strValue;
        abi.abiOut(data, strValue);
        auto entries = table->select(HELLOWORLD_KEY_FIELD_NAME, table->newCondition());
        auto entry = table->newEntry();
        entry->setField(HELLOWORLD_KEY_FIELD, HELLOWORLD_KEY_FIELD_NAME);
        entry->setField(HELLOWORLD_VALUE_FIELD, strValue);

        int count = 0;
        if (0u != entries->size())
        {  // update
            count = table->update(HELLOWORLD_KEY_FIELD_NAME, entry, table->newCondition(),
                std::make_shared<AccessOptions>(_origin));
        }
        else
        {  // insert
            count = table->insert(
                HELLOWORLD_KEY_FIELD_NAME, entry, std::make_shared<AccessOptions>(_origin));
        }

        if (count == storage::CODE_NO_AUTHORIZED)
        {  //  permission denied
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("HelloWorldPrecompiled") << LOG_DESC("set")
                                   << LOG_DESC("permission denied");
        }
        getErrorCodeOut(out, count);
    }
    else
    {  // unknown function call
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("HelloWorldPrecompiled") << LOG_DESC(" unknown func ")
                               << LOG_KV("func", func);
        out = abi.abiIn("", u256(CODE_UNKNOW_FUNCTION_CALL));
    }

    return out;
}
