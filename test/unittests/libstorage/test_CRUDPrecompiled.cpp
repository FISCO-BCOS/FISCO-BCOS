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
#include "Common.h"
#include "libstoragestate/StorageStateFactory.h"
#include <libblockverifier/ExecutiveContextFactory.h>
#include <libdevcrypto/Common.h>
#include <libethcore/ABI.h>
#include <libprecompiled/CRUDPrecompiled.h>
#include <libstorage/MemoryTable.h>
#include <libstorage/TableFactoryPrecompiled.h>
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::storage;
using namespace dev::storagestate;
using namespace dev::precompiled;

namespace test_CRUDPrecompiled
{
class MockMemoryTableFactory : public dev::storage::MemoryTableFactory
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
        factory.setStateStorage(storage);
        factory.setStateFactory(storageStateFactory);
        factory.initExecutiveContext(blockInfo, h256(0), context);
        memoryTableFactory = context->getMemoryTableFactory();

        tableFactoryPrecompiled = std::make_shared<dev::blockverifier::TableFactoryPrecompiled>();
        tableFactoryPrecompiled->setMemoryTableFactory(memoryTableFactory);
        crudPrecompiled = context->getPrecompiled(Address(0x1002));
    }

    ~CRUDPrecompiledFixture() {}

    ExecutiveContext::Ptr context;
    MemoryTableFactory::Ptr memoryTableFactory;
    Precompiled::Ptr crudPrecompiled;
    dev::blockverifier::TableFactoryPrecompiled::Ptr tableFactoryPrecompiled;
    BlockInfo blockInfo;
};

BOOST_FIXTURE_TEST_SUITE(CRUDPrecompiled, CRUDPrecompiledFixture)

BOOST_AUTO_TEST_CASE(CRUD)
{
    // createTable
    dev::eth::ContractABI abi;
    std::string tableName = "t_test", key = "name", valueField = "item_id,item_name";
    bytes param = abi.abiIn("createTable(string,string,string)", tableName, key, valueField);
    bytes out = tableFactoryPrecompiled->call(context, bytesConstRef(&param));
    u256 createResult = 0;
    abi.abiOut(&out, createResult);
    BOOST_TEST(createResult == 1u);

    // insert
    std::string insertFunc = "insert(string,string,string,string)";
    std::string entryStr = "{\"item_id\":\"1\",\"name\":\"fruit\",\"item_name\":\"apple\"}";
    param.clear();
    out.clear();
    param = abi.abiIn(insertFunc, tableName, key, entryStr, "");
    out = crudPrecompiled->call(context, bytesConstRef(&param));
    u256 insertResult = 0;
    abi.abiOut(&out, insertResult);
    BOOST_TEST(insertResult == 1u);

    // select
    std::string selectFunc = "select(string,string,string,string)";
    std::string conditonStr = "{\"item_id\":{\"eq\":\"1\"},\"lit\":{\"lit\":\"0,1\"}}";
    param.clear();
    out.clear();
    param = abi.abiIn(selectFunc, tableName, key, conditonStr, "");
    out = crudPrecompiled->call(context, bytesConstRef(&param));
    std::string selectResult;
    abi.abiOut(&out, selectResult);
    Json::Value entryJson;
    Json::Reader reader;
    reader.parse(selectResult, entryJson);
    BOOST_TEST(entryJson.size() == 1);

    // update
    std::string updateFunc = "update(string,string,string,string,string)";
    entryStr = "{\"item_id\":\"1\",\"name\":\"fruit\",\"item_name\":\"orange\"}";
    conditonStr = "{\"item_id\":{\"eq\":\"1\"}}";
    param.clear();
    out.clear();
    param = abi.abiIn(updateFunc, tableName, key, entryStr, conditonStr, "");
    out = crudPrecompiled->call(context, bytesConstRef(&param));
    u256 updateResult = 0;
    abi.abiOut(&out, updateResult);
    BOOST_TEST(updateResult == 1u);

    // remove
    std::string removeFunc = "remove(string,string,string,string)";
    conditonStr = "{\"item_id\":{\"eq\":\"1\"}}";
    param.clear();
    out.clear();
    param = abi.abiIn(removeFunc, tableName, key, conditonStr, "");
    out = crudPrecompiled->call(context, bytesConstRef(&param));
    u256 removeResult = 0;
    abi.abiOut(&out, removeResult);
    BOOST_TEST(removeResult == 1u);

    // errorFunc
    std::string errorFunc = "errorFunc(string,string,string,string)";
    param = abi.abiIn(errorFunc, tableName, key, conditonStr, "");
    out = crudPrecompiled->call(context, bytesConstRef(&param));
}

BOOST_AUTO_TEST_CASE(toString)
{
    BOOST_TEST(crudPrecompiled->toString() == "CRUD");
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_CRUDPrecompiled
