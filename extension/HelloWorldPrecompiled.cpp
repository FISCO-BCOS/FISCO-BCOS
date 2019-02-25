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
#pragma GCC diagnostic ignored "-Wunused-parameter"

#include "HelloWorldPrecompiled.h"
#include <json_spirit/JsonSpiritHeaders.h>
#include <libdevcore/easylog.h>
#include <libethcore/ABI.h>
#include <libstorage/EntriesPrecompiled.h>
#include <libstorage/TableFactoryPrecompiled.h>

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
const std::string HELLO_WORLD_TABLE_NAME = "_ext_hello_world_";
// key field
const std::string HELLOWORLD_KEY_FIELD_NAME = "hello_key";
// value field
const std::string HELLOWORLD_VALUE_FIELD_VALUE = "hello_value";

// get interface
const char* const HELLO_WORLD_METHOD_GET = "get()";
// set interface
const char* const HELLO_WORLD_METHOD_SET = "set(string)";

HelloWorldPrecompiled::HelloWorldPrecompiled()
{
    name2Selector[HELLO_WORLD_METHOD_GET] = getFuncSelector(HELLO_WORLD_METHOD_GET);
    name2Selector[HELLO_WORLD_METHOD_SET] = getFuncSelector(HELLO_WORLD_METHOD_SET);
}

std::string HelloWorldPrecompiled::toString(dev::blockverifier::ExecutiveContext::Ptr)
{
    return "HelloWorld";
}

Table::Ptr HelloWorldPrecompiled::openTable(
    dev::blockverifier::ExecutiveContext::Ptr context, Address const& origin)
{
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("HelloWorldPrecompiled") << LOG_DESC("openTable");

    TableFactoryPrecompiled::Ptr tableFactoryPrecompiled =
        std::dynamic_pointer_cast<TableFactoryPrecompiled>(
            context->getPrecompiled(Address(0x1001)));
    auto table =
        tableFactoryPrecompiled->getmemoryTableFactory()->openTable(HELLO_WORLD_TABLE_NAME);
    if (!table)
    {
        // table is not exist, create it.
        table =
            tableFactoryPrecompiled->getmemoryTableFactory()->createTable(HELLO_WORLD_TABLE_NAME,
                HELLOWORLD_KEY_FIELD_NAME, HELLOWORLD_VALUE_FIELD_VALUE, true, origin);
    }
    return table;
}

bytes HelloWorldPrecompiled::call(
    dev::blockverifier::ExecutiveContext::Ptr context, bytesConstRef param, Address const& origin)
{
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("HelloWorldPrecompiled") << LOG_DESC("call")
                           << LOG_KV("param", toHex(param));

    // parse function name
    uint32_t func = getParamFunc(param);
    bytesConstRef data = getParamData(param);

    bytes out;
    if (func == name2Selector[HELLO_WORLD_METHOD_GET])
    {  // get() function call
        get(context, data, origin, out);
    }
    else if (func == name2Selector[HELLO_WORLD_METHOD_SET])
    {  // set(string) function call
        set(context, data, origin, out);
    }
    else
    {  // unkown function call
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("HelloWorldPrecompiled") << LOG_DESC(" unkown func ")
                               << LOG_KV("func", func);
    }

    PRECOMPILED_LOG(TRACE) << LOG_BADGE("HelloWorldPrecompiled") << LOG_DESC("call")
                           << LOG_DESC("end");

    return out;
}

// call HelloWorld get interface
void HelloWorldPrecompiled::get(dev::blockverifier::ExecutiveContext::Ptr context,
    bytesConstRef data, Address const& origin, bytes& out)
{
    Table::Ptr table = openTable(context, origin);
    if (!table)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("HelloWorldPrecompiled") << LOG_DESC("get")
                               << LOG_DESC("open table failed.");
        return;
    }

    // default retMsg
    std::string retValue = "Hello World!";
    auto entries = table->select(HELLOWORLD_KEY_FIELD_NAME, table->newCondition());
    if (entries.get() && (0u != entries->size()))
    {  // exist
        auto entry = entries->get(0);
        retValue = entry->getField(HELLOWORLD_VALUE_FIELD_VALUE);
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("HelloWorldPrecompiled") << LOG_DESC("get")
                               << LOG_KV("value", retValue);
    }

    dev::eth::ContractABI abi;
    out = abi.abiIn("", retValue);
}

// call HelloWorld set interface
void HelloWorldPrecompiled::set(dev::blockverifier::ExecutiveContext::Ptr context,
    bytesConstRef data, Address const& origin, bytes& out)
{
    Table::Ptr table = openTable(context, origin);
    if (!table)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("HelloWorldPrecompiled") << LOG_DESC("set")
                               << LOG_DESC("open table failed.");
        return;
    }
    dev::eth::ContractABI abi;
    std::string strValue;

    abi.abiOut(data, strValue);

    auto entries = table->select(HELLOWORLD_KEY_FIELD_NAME, table->newCondition());
    auto entry = table->newEntry();
    entry->setField(HELLOWORLD_KEY_FIELD_NAME, HELLOWORLD_KEY_FIELD_NAME);
    entry->setField(HELLOWORLD_VALUE_FIELD_VALUE, strValue);

    int count = 0;
    if (entries.get() && (0u != entries->size()))
    {  // update
        count = table->update(HELLOWORLD_KEY_FIELD_NAME, entry, table->newCondition(),
            std::make_shared<AccessOptions>(origin));
    }
    else
    {  // insert
        count = table->insert(
            HELLOWORLD_KEY_FIELD_NAME, entry, std::make_shared<AccessOptions>(origin));
    }

    if (count == CODE_NO_AUTHORIZED)
    {  //  permission denied
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("HelloWorldPrecompiled") << LOG_DESC("set")
                               << LOG_DESC("non-authorized");
    }
}
