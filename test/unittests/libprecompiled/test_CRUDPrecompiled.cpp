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

#include "../libstorage/MemoryStorage.h"
#include "libstoragestate/StorageStateFactory.h"
#include <json/json.h>
#include <libblockverifier/ExecutiveContextFactory.h>
#include <libdevcrypto/Common.h>
#include <libethcore/ABI.h>
#include <libprecompiled/CRUDPrecompiled.h>
#include <libprecompiled/TableFactoryPrecompiled.h>
#include <libstorage/MemoryTableFactoryFactory.h>
#include <boost/test/unit_test.hpp>

using namespace std;
using namespace bcos;
using namespace bcos::blockverifier;
using namespace bcos::storage;
using namespace bcos::storagestate;
using namespace bcos::precompiled;

namespace test_CRUDPrecompiled
{
class MockMemoryTableFactory : public bcos::storage::MemoryTableFactory
{
public:
    virtual ~MockMemoryTableFactory(){};
};
struct CRUDPrecompiledFixture
{
    CRUDPrecompiledFixture()
    {
        context = std::make_shared<ExecutiveContext>();
        blockInfo.hash = h256(0);
        blockInfo.number = 0;
        context->setBlockInfo(blockInfo);

        auto storage = std::make_shared<MemoryStorage>();
        auto storageStateFactory = std::make_shared<StorageStateFactory>(h256(0));
        ExecutiveContextFactory factory;
        auto tableFactoryFactory = std::make_shared<MemoryTableFactoryFactory>();
        factory.setStateStorage(storage);
        factory.setStateFactory(storageStateFactory);
        factory.setTableFactoryFactory(tableFactoryFactory);
        factory.initExecutiveContext(blockInfo, h256(0), context);
        memoryTableFactory = context->getMemoryTableFactory();

        tableFactoryPrecompiled = std::make_shared<bcos::precompiled::TableFactoryPrecompiled>();
        tableFactoryPrecompiled->setMemoryTableFactory(memoryTableFactory);
        crudPrecompiled = context->getPrecompiled(Address(0x1002));

        auto precompiledGasFactory = std::make_shared<bcos::precompiled::PrecompiledGasFactory>(0);
        auto precompiledExecResultFactory = std::make_shared<PrecompiledExecResultFactory>();
        precompiledExecResultFactory->setPrecompiledGasFactory(precompiledGasFactory);
        crudPrecompiled->setPrecompiledExecResultFactory(precompiledExecResultFactory);
        tableFactoryPrecompiled->setPrecompiledExecResultFactory(precompiledExecResultFactory);
    }

    ~CRUDPrecompiledFixture() {}

    ExecutiveContext::Ptr context;
    TableFactory::Ptr memoryTableFactory;
    Precompiled::Ptr crudPrecompiled;
    bcos::precompiled::TableFactoryPrecompiled::Ptr tableFactoryPrecompiled;
    BlockInfo blockInfo;
};

BOOST_FIXTURE_TEST_SUITE(CRUDPrecompiled, CRUDPrecompiledFixture)

BOOST_AUTO_TEST_CASE(CRUD)
{
    // createTable
    bcos::eth::ContractABI abi;
    std::string tableName = "t_test", tableName2 = "t_demo", key = "name",
                valueField = "item_id,item_name";
    bytes param = abi.abiIn("createTable(string,string,string)", tableName, key, valueField);
    auto callResult = tableFactoryPrecompiled->call(context, bytesConstRef(&param));
    bytes out = callResult->execResult();
    u256 createResult = 0;
    abi.abiOut(&out, createResult);
    BOOST_TEST(createResult == 0u);

    // desc
    param.clear();
    out.clear();
    param = abi.abiIn("desc(string)", tableName);
    callResult = crudPrecompiled->call(context, bytesConstRef(&param));
    out = callResult->execResult();
    string keyField, valueField2;
    abi.abiOut(&out, keyField, valueField2);
    BOOST_TEST(keyField == "name");
    BOOST_TEST(valueField == valueField2);

    // insert
    std::string insertFunc = "insert(string,string,string,string)";
    std::string entryStr = "{\"item_id\":\"1\",\"name\":\"fruit\",\"item_name\":\"apple\"}";
    param.clear();
    out.clear();
    param = abi.abiIn(insertFunc, tableName, key, entryStr, std::string(""));
    callResult = crudPrecompiled->call(context, bytesConstRef(&param));
    out = callResult->execResult();
    u256 insertResult = 0;
    abi.abiOut(&out, insertResult);
    BOOST_TEST(insertResult == 1u);

    // insert table not exist
    entryStr = "{\"item_id\":\"1\",\"name\":\"fruit\",\"item_name\":\"apple\"}";
    param.clear();
    out.clear();
    param = abi.abiIn(insertFunc, tableName2, key, entryStr, std::string(""));
    callResult = crudPrecompiled->call(context, bytesConstRef(&param));
    out = callResult->execResult();
    insertResult = 0;
    abi.abiOut(&out, insertResult);
    BOOST_TEST(insertResult == CODE_TABLE_NOT_EXIST);

    // insert entry error
    entryStr = "{\"item_id\"1\",\"name\":\"fruit\",\"item_name\":\"apple\"}";
    param.clear();
    out.clear();
    param = abi.abiIn(insertFunc, tableName, key, entryStr, std::string(""));
    callResult = crudPrecompiled->call(context, bytesConstRef(&param));
    out = callResult->execResult();
    insertResult = 0;
    abi.abiOut(&out, insertResult);
    BOOST_TEST(insertResult == CODE_PARSE_ENTRY_ERROR);

    // select
    std::string selectFunc = "select(string,string,string,string)";
    std::string conditionStr = "{\"item_id\":{\"eq\":\"1\"},\"limit\":{\"limit\":\"0,1\"}}";
    param.clear();
    out.clear();
    param = abi.abiIn(selectFunc, tableName, key, conditionStr, std::string(""));
    callResult = crudPrecompiled->call(context, bytesConstRef(&param));
    out = callResult->execResult();
    std::string selectResult;
    abi.abiOut(&out, selectResult);
    Json::Value entryJson;
    Json::Reader reader;
    reader.parse(selectResult, entryJson);
    BOOST_TEST(entryJson.size() == 1);

    // select table not exist
    param.clear();
    out.clear();
    param = abi.abiIn(selectFunc, tableName2, key, conditionStr, std::string(""));
    callResult = crudPrecompiled->call(context, bytesConstRef(&param));
    out = callResult->execResult();
    u256 selectResult2 = 0;
    abi.abiOut(&out, selectResult2);
    BOOST_TEST(selectResult2 == CODE_TABLE_NOT_EXIST);

    // select condition error
    conditionStr = "{\"item_id\":\"eq\":\"1\"},\"limit\":{\"limit\":\"0,1\"}}";
    param.clear();
    out.clear();
    param = abi.abiIn(selectFunc, tableName, key, conditionStr, std::string(""));
    callResult = crudPrecompiled->call(context, bytesConstRef(&param));
    out = callResult->execResult();
    selectResult2 = 0;
    abi.abiOut(&out, selectResult2);
    BOOST_TEST(selectResult2 == CODE_PARSE_CONDITION_ERROR);

    // update
    std::string updateFunc = "update(string,string,string,string,string)";
    entryStr = "{\"item_id\":\"1\",\"name\":\"fruit\",\"item_name\":\"orange\"}";
    conditionStr = "{\"item_id\":{\"eq\":\"1\"}}";
    param.clear();
    out.clear();
    param = abi.abiIn(updateFunc, tableName, key, entryStr, conditionStr, std::string(""));
    callResult = crudPrecompiled->call(context, bytesConstRef(&param));
    out = callResult->execResult();
    u256 updateResult = 0;
    abi.abiOut(&out, updateResult);
    BOOST_TEST(updateResult == CODE_INVALID_UPDATE_TABLE_KEY);

    updateFunc = "update(string,string,string,string,string)";
    entryStr = "{\"item_id\":\"1\",\"item_name\":\"orange\"}";
    conditionStr = "{\"item_id\":{\"eq\":\"1\"}}";
    param.clear();
    out.clear();
    param = abi.abiIn(updateFunc, tableName, key, entryStr, conditionStr, std::string(""));
    callResult = crudPrecompiled->call(context, bytesConstRef(&param));
    out = callResult->execResult();
    updateResult = 0;
    abi.abiOut(&out, updateResult);
    BOOST_TEST(updateResult == 1u);

    // update table not exist
    entryStr = "{\"item_id\":\"1\",\"name\":\"fruit\",\"item_name\":\"orange\"}";
    conditionStr = "{\"item_id\":{\"eq\":\"1\"}}";
    param.clear();
    out.clear();
    param = abi.abiIn(updateFunc, tableName2, key, entryStr, conditionStr, std::string(""));
    callResult = crudPrecompiled->call(context, bytesConstRef(&param));
    out = callResult->execResult();
    updateResult = 0;
    abi.abiOut(&out, updateResult);
    BOOST_TEST(updateResult == CODE_TABLE_NOT_EXIST);

    // update entry error
    entryStr = "{\"item_id\"1\",\"name\":\"fruit\",\"item_name\":\"apple\"}";
    conditionStr = "{\"item_id\":{\"eq\":\"1\"}}";
    param.clear();
    out.clear();
    param = abi.abiIn(updateFunc, tableName, key, entryStr, conditionStr, std::string(""));
    callResult = crudPrecompiled->call(context, bytesConstRef(&param));
    out = callResult->execResult();
    updateResult = 0;
    abi.abiOut(&out, updateResult);
    BOOST_TEST(updateResult == CODE_PARSE_ENTRY_ERROR);

    // update condition error
    entryStr = "{\"item_id\":\"1\",\"item_name\":\"orange\"}";
    conditionStr = "{\"item_id\"\"eq\":\"1\"}}";
    param.clear();
    out.clear();
    param = abi.abiIn(updateFunc, tableName, key, entryStr, conditionStr, std::string(""));
    callResult = crudPrecompiled->call(context, bytesConstRef(&param));
    out = callResult->execResult();
    updateResult = 0;
    abi.abiOut(&out, updateResult);
    BOOST_TEST(updateResult == CODE_PARSE_CONDITION_ERROR);

    // remove
    std::string removeFunc = "remove(string,string,string,string)";
    conditionStr = "{\"item_id\":{\"eq\":\"1\"}}";
    param.clear();
    out.clear();
    param = abi.abiIn(removeFunc, tableName, key, conditionStr, std::string(""));
    callResult = crudPrecompiled->call(context, bytesConstRef(&param));
    out = callResult->execResult();
    u256 removeResult = 0;
    abi.abiOut(&out, removeResult);
    BOOST_TEST(removeResult == 1u);

    // remove table not exist
    conditionStr = "{\"item_id\":{\"eq\":\"1\"}}";
    param.clear();
    out.clear();
    param = abi.abiIn(removeFunc, tableName2, key, conditionStr, std::string(""));
    callResult = crudPrecompiled->call(context, bytesConstRef(&param));
    out = callResult->execResult();
    removeResult = 0;
    abi.abiOut(&out, removeResult);
    BOOST_TEST(removeResult == CODE_TABLE_NOT_EXIST);

    // remove condition error
    conditionStr = "{\"item_id\"\"eq\":\"1\"}}";
    param.clear();
    out.clear();
    param = abi.abiIn(removeFunc, tableName, key, conditionStr, std::string(""));
    callResult = crudPrecompiled->call(context, bytesConstRef(&param));
    out = callResult->execResult();
    removeResult = 0;
    abi.abiOut(&out, removeResult);
    BOOST_TEST(removeResult == CODE_PARSE_CONDITION_ERROR);

    // remove condition operation undefined
    conditionStr = "{\"item_id\":{\"eqq\":\"1\"}}";
    param.clear();
    out.clear();
    param = abi.abiIn(removeFunc, tableName, key, conditionStr, std::string(""));
    callResult = crudPrecompiled->call(context, bytesConstRef(&param));
    out = callResult->execResult();
    removeResult = 0;
    abi.abiOut(&out, removeResult);
    BOOST_TEST(removeResult == CODE_CONDITION_OPERATION_UNDEFINED);

    // function not exist
    std::string errorFunc = "errorFunc(string,string,string,string)";
    param = abi.abiIn(errorFunc, tableName, key, conditionStr, std::string(""));
    callResult = crudPrecompiled->call(context, bytesConstRef(&param));
    out = callResult->execResult();
    u256 funcResult = 0;
    abi.abiOut(&out, funcResult);
    BOOST_TEST(funcResult == CODE_UNKNOW_FUNCTION_CALL);
}

BOOST_AUTO_TEST_CASE(toString)
{
    BOOST_TEST(crudPrecompiled->toString() == "CRUD");
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_CRUDPrecompiled
