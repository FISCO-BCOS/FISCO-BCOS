/**
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
 *
 * @brief: unit test for Authority
 *
 * @file test_AuthorityPrecompiled.cpp
 * @author: caryliao
 * @date 20181212
 */

#include "../libstorage/MemoryStorage.h"
#include <json/json.h>
#include <libblockverifier/ExecutiveContextFactory.h>
#include <libdevcrypto/Common.h>
#include <libprecompiled/PermissionPrecompiled.h>
#include <libprotocol/ContractABICodec.h>
#include <libstorage/MemoryTableFactoryFactory.h>
#include <libstoragestate/StorageStateFactory.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>

using namespace std;
using namespace bcos;
using namespace bcos::blockverifier;
using namespace bcos::storage;
using namespace bcos::storagestate;
using namespace bcos::precompiled;
using namespace bcos::test;

namespace test_AuthorityPrecompiled
{
struct AuthorityPrecompiledFixture
{
    AuthorityPrecompiledFixture(bool useSMCrypto = false)
    {
        m_useSMCrypto = useSMCrypto;
        blockInfo.hash = h256(0);
        blockInfo.number = 0;
        // clear global name2Selector cache
        clearName2SelectCache();
        context = std::make_shared<ExecutiveContext>();
        ExecutiveContextFactory factory;
        auto storage = std::make_shared<MemoryStorage>();
        auto storageStateFactory = std::make_shared<StorageStateFactory>(h256(0));
        auto tableFactoryFactory = std::make_shared<MemoryTableFactoryFactory>();
        factory.setStateStorage(storage);
        factory.setStateFactory(storageStateFactory);
        factory.setTableFactoryFactory(tableFactoryFactory);
        factory.initExecutiveContext(blockInfo, h256(0), context);
        authorityPrecompiled = context->getPrecompiled(Address(0x1005));
        memoryTableFactory = context->getMemoryTableFactory();

        auto precompiledGasFactory = std::make_shared<bcos::precompiled::PrecompiledGasFactory>(0);
        auto precompiledExecResultFactory = std::make_shared<PrecompiledExecResultFactory>();
        precompiledExecResultFactory->setPrecompiledGasFactory(precompiledGasFactory);
        authorityPrecompiled->setPrecompiledExecResultFactory(precompiledExecResultFactory);
    }

    ~AuthorityPrecompiledFixture()
    {
        // clear global name2Selector cache
        clearName2SelectCache();
    }

    ExecutiveContext::Ptr context;
    TableFactory::Ptr memoryTableFactory;
    Precompiled::Ptr authorityPrecompiled;
    BlockInfo blockInfo;
    bcos::VERSION m_version;
    std::string m_supportedVersion;
    bool m_useSMCrypto;
};

struct SM_AuthorityPrecompiledFixture : public SM_CryptoTestFixture,
                                        public AuthorityPrecompiledFixture
{
    SM_AuthorityPrecompiledFixture() : SM_CryptoTestFixture(), AuthorityPrecompiledFixture(true) {}
};

BOOST_FIXTURE_TEST_SUITE(test_AuthorityPrecompiled, AuthorityPrecompiledFixture)

BOOST_AUTO_TEST_CASE(insert)
{
    // first insert
    protocol::ContractABICodec abi;
    std::string tableName = "t_test";
    std::string addr = "0x420f853b49838bd3e9466c85a4cc3428c960dde2";
    bytes in = abi.abiIn("insert(string,string)", tableName, addr);
    auto callResult = authorityPrecompiled->call(context, bytesConstRef(&in));
    bytes out = callResult->execResult();
    // query
    auto table = memoryTableFactory->openTable(SYS_ACCESS_TABLE);
    auto entries = table->select(precompiled::getTableName(tableName), table->newCondition());
    BOOST_TEST(entries->size() == 1u);

    // insert again with same item
    callResult = authorityPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
    // query
    entries = table->select(precompiled::getTableName(tableName), table->newCondition());
    BOOST_TEST(entries->size() == 1u);

    // insert new item with same table name, but different address
    addr = "0xa94f5374fce5edbc8e2a8697c15331677e6ebf0b";
    in = abi.abiIn("insert(string,string)", tableName, addr);
    callResult = authorityPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
    // query
    entries = table->select(precompiled::getTableName(tableName), table->newCondition());
    BOOST_TEST(entries->size() == 2u);
}

BOOST_AUTO_TEST_CASE(insert_overflow)
{
    // first insert
    protocol::ContractABICodec abi;
    std::string tableName = "66666012345678901234567890123456789012345678901234567890123456789";
    std::string addr = "0x420f853b49838bd3e9466c85a4cc3428c960dde2";
    bytes in = abi.abiIn("insert(string,string)", tableName, addr);
    auto callResult = authorityPrecompiled->call(context, bytesConstRef(&in));
    bytes out = callResult->execResult();
    s256 errCode;
    abi.abiOut(&out, errCode);
    BOOST_TEST(errCode == CODE_TABLE_NAME_OVERFLOW);
}

BOOST_AUTO_TEST_CASE(remove)
{
    // first insert
    protocol::ContractABICodec abi;
    std::string tableName = "t_test";
    std::string addr = "0x420f853b49838bd3e9466c85a4cc3428c960dde2";
    bytes in = abi.abiIn("insert(string,string)", tableName, addr);
    auto callResult = authorityPrecompiled->call(context, bytesConstRef(&in));
    bytes out = callResult->execResult();
    // query
    auto table = memoryTableFactory->openTable(SYS_ACCESS_TABLE);
    auto entries = table->select(precompiled::getTableName(tableName), table->newCondition());
    BOOST_TEST(entries->size() == 1u);

    // remove
    in = abi.abiIn("remove(string,string)", tableName, addr);
    callResult = authorityPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();

    // query
    table = memoryTableFactory->openTable(SYS_ACCESS_TABLE);
    Condition::Ptr condition = table->newCondition();
    condition->EQ(STATUS, "1");
    entries = table->select(precompiled::getTableName(tableName), condition);
    BOOST_TEST(entries->size() == 0u);

    // remove not exist entry
    tableName = "t_ok";
    in = abi.abiIn("remove(string,string)", tableName, addr);
    authorityPrecompiled->call(context, bytesConstRef(&in));
    BOOST_TEST(entries->size() == 0u);

    // add committee member permission
    auto origin = Address(addr);
    in = abi.abiIn("insert(string,string)", SYS_ACCESS_TABLE, addr);
    callResult = authorityPrecompiled->call(context, bytesConstRef(&in), origin);
    out = callResult->execResult();
    s256 ret = 0;
    abi.abiOut(&out, ret);
    BOOST_TEST(ret == CODE_COMMITTEE_PERMISSION);

    in = abi.abiIn("insert(string,string)", SYS_CONSENSUS, addr);
    callResult = authorityPrecompiled->call(context, bytesConstRef(&in), origin);
    out = callResult->execResult();
    ret = 0;
    abi.abiOut(&out, ret);
    BOOST_TEST(ret == 1);

    in = abi.abiIn("remove(string,string)", SYS_CONSENSUS, addr);
    callResult = authorityPrecompiled->call(context, bytesConstRef(&in), origin);
    out = callResult->execResult();
    ret = 0;
    abi.abiOut(&out, ret);
    BOOST_TEST(ret == 1);

    in = abi.abiIn("insert(string,string)", SYS_CONSENSUS, addr);
    callResult = authorityPrecompiled->call(context, bytesConstRef(&in), origin);
    out = callResult->execResult();
    ret = 0;
    abi.abiOut(&out, ret);
    BOOST_TEST(ret == 1);
}

BOOST_AUTO_TEST_CASE(grantWrite)
{
    // first insert
    protocol::ContractABICodec abi;
    std::string tableName = "t_test";
    std::string addr = "0x420f853b49838bd3e9466c85a4cc3428c960dde2";
    bytes in = abi.abiIn("insert(string,string)", tableName, addr);
    auto callResult = authorityPrecompiled->call(context, bytesConstRef(&in));
    bytes out = callResult->execResult();

    memoryTableFactory->setBlockNum(5);
    // have committee and operator, can't grantWrite
    m_version = g_BCOSConfig.version();
    m_supportedVersion = g_BCOSConfig.supportedVersion();
    auto origin = Address(addr);
    auto acTable = memoryTableFactory->openTable(SYS_ACCESS_TABLE);
    auto entry = acTable->newEntry();
    entry->setField(SYS_AC_TABLE_NAME, SYS_ACCESS_TABLE);
    entry->setField(SYS_AC_ADDRESS, addr);
    entry->setField(SYS_AC_ENABLENUM, "0");
    acTable->insert(SYS_ACCESS_TABLE, entry, std::make_shared<AccessOptions>(origin, false));
    auto tableInfo = acTable->tableInfo();
    tableInfo->authorizedAddress.emplace_back(origin);
    // create contract table
    tableName = string("c_420f853b49838bd3e9466c85a4cc3428c960dde4");
    Address contract("420f853b49838bd3e9466c85a4cc3428c960dde4");
    std::string addr2 = "0x420f853b49838bd3e9466c85a4cc3428c960dde3";
    auto origin2 = Address("420f853b49838bd3e9466c85a4cc3428c960dde3");
    auto table = memoryTableFactory->createTable(tableName, "key", "value", false);
    entry = table->newEntry();
    entry->setField("key", "authority");
    entry->setField("value", origin2.hex());
    table->insert("authority", entry);

    auto origin3 = Address("0x420f853b49838bd3e9466c85a4cc3428c960dde5");
    in = abi.abiIn("grantWrite(address,address)", contract, origin3);
    callResult = authorityPrecompiled->call(context, bytesConstRef(&in), origin2);
    out = callResult->execResult();
    s256 ret = 0;
    abi.abiOut(&out, ret);
    BOOST_TEST(ret == 1);
    callResult = authorityPrecompiled->call(context, bytesConstRef(&in), origin2);
    out = callResult->execResult();
    ret = 0;
    abi.abiOut(&out, ret);
    BOOST_TEST(ret == CODE_TABLE_AND_ADDRESS_EXIST);
    in = abi.abiIn("revokeWrite(address,address)", contract, origin3);
    callResult = authorityPrecompiled->call(context, bytesConstRef(&in), origin2);
    out = callResult->execResult();
    ret = 0;
    abi.abiOut(&out, ret);
    BOOST_TEST(ret == 1);
    callResult = authorityPrecompiled->call(context, bytesConstRef(&in), origin2);
    out = callResult->execResult();
    ret = 0;
    abi.abiOut(&out, ret);
    BOOST_TEST(ret == CODE_TABLE_AND_ADDRESS_NOT_EXIST);
}

BOOST_AUTO_TEST_CASE(grantWrite_contract)
{
    // first insert
    protocol::ContractABICodec abi;
    Address contractAddress("0x420f853b49838bd3e9466c85a4cc3428c960dde1");
    std::string tableName = precompiled::getContractTableName(contractAddress);
    Address addr("0x420f853b49838bd3e9466c85a4cc3428c960dde2");
    bytes in = abi.abiIn("grantWrite(address,address)", contractAddress, addr);
    auto callResult = authorityPrecompiled->call(context, bytesConstRef(&in));
    bytes out = callResult->execResult();
    s256 ret = 0;
    abi.abiOut(&out, ret);
    BOOST_TEST(ret == CODE_CONTRACT_NOT_EXIST);
    auto table = memoryTableFactory->openTable(SYS_TABLES);
    auto entry = table->newEntry();
    entry->setField("table_name", tableName);
    entry->setField("key_field", "key");
    entry->setField("value_field", "value");
    auto result =
        table->insert(tableName, entry, std::make_shared<AccessOptions>(Address(), false));
    BOOST_TEST(result == 1);
    callResult = authorityPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
    ret = 0;
    abi.abiOut(&out, ret);
    BOOST_TEST(ret == CODE_NO_AUTHORIZED);

    // query
    auto entries = table->select(tableName, table->newCondition());
    BOOST_TEST(entries->size() == 1u);

    // insert again with same item
    callResult = authorityPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
    // query
    table = memoryTableFactory->openTable(SYS_ACCESS_TABLE);
    entries = table->select(tableName, table->newCondition());
    BOOST_TEST(entries->size() == 0u);

    // insert new item with same table name, but different address
    Address addr2("0xa94f5374fce5edbc8e2a8697c15331677e6ebf0b");
    in = abi.abiIn("grantWrite(address,address)", contractAddress, addr2);
    callResult = authorityPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
    ret = 0;
    abi.abiOut(&out, ret);
    BOOST_TEST(ret == CODE_NO_AUTHORIZED);

    // query
    table = memoryTableFactory->openTable(SYS_ACCESS_TABLE);
    entries = table->select(tableName, table->newCondition());
    BOOST_TEST(entries->size() == 0u);
    // remove
    in = abi.abiIn("revokeWrite(address,address)", contractAddress, addr2);
    callResult = authorityPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
    ret = 0;
    abi.abiOut(&out, ret);
    BOOST_TEST(ret == CODE_TABLE_AND_ADDRESS_NOT_EXIST);

    // queryByName by a existing key
    in = abi.abiIn("queryPermission(address)", contractAddress);
    callResult = authorityPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
    std::string retStr;
    abi.abiOut(&out, retStr);

    Json::Value retJson;
    Json::Reader reader;
    BOOST_TEST(reader.parse(retStr, retJson) == true);
    BOOST_TEST(retJson.size() == 0);
}

BOOST_AUTO_TEST_CASE(queryByName)
{
    // insert
    protocol::ContractABICodec abi;
    std::string tableName = "t_test";
    std::string addr = "0x420f853b49838bd3e9466c85a4cc3428c960dde2";
    bytes in = abi.abiIn("insert(string,string)", tableName, addr);

    auto callResult = authorityPrecompiled->call(context, bytesConstRef(&in));
    bytes out = callResult->execResult();

    // queryByName by a existing key
    in = abi.abiIn("queryByName(string)", tableName);
    callResult = authorityPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
    std::string retStr;
    abi.abiOut(&out, retStr);

    Json::Value retJson;
    Json::Reader reader;
    BOOST_TEST(reader.parse(retStr, retJson) == true);
    BOOST_TEST(retJson.size() == 1);

    std::string keyName = "test";
    // queryByName by a no existing key
    in = abi.abiIn("queryByName(string)", keyName);
    callResult = authorityPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
    abi.abiOut(&out, retStr);
    BOOST_TEST(reader.parse(retStr, retJson) == true);
    BOOST_TEST(retJson.size() == 0);
}

BOOST_AUTO_TEST_CASE(error_func)
{
    // insert
    protocol::ContractABICodec abi;
    std::string tableName = "t_test";
    std::string addr = "0x420f853b49838bd3e9466c85a4cc3428c960dde2";
    bytes in = abi.abiIn("insert(string)", tableName, addr);
    auto callResult = authorityPrecompiled->call(context, bytesConstRef(&in));
    bytes out = callResult->execResult();
}

BOOST_AUTO_TEST_CASE(toString)
{
    BOOST_TEST(authorityPrecompiled->toString() == "Permission");
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE(SM_test_AuthorityPrecompiled, SM_AuthorityPrecompiledFixture)

BOOST_AUTO_TEST_CASE(insert)
{
    // first insert
    protocol::ContractABICodec abi;
    std::string tableName = "t_test";
    std::string addr = "0x420f853b49838bd3e9466c85a4cc3428c960dde2";
    bytes in = abi.abiIn("insert(string,string)", tableName, addr);
    auto callResult = authorityPrecompiled->call(context, bytesConstRef(&in));
    bytes out = callResult->execResult();
    // query
    auto table = memoryTableFactory->openTable(SYS_ACCESS_TABLE);
    auto entries = table->select(precompiled::getTableName(tableName), table->newCondition());
    BOOST_TEST(entries->size() == 1u);

    // insert again with same item
    callResult = authorityPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
    // query
    entries = table->select(precompiled::getTableName(tableName), table->newCondition());
    BOOST_TEST(entries->size() == 1u);

    // insert new item with same table name, but different address
    addr = "0xa94f5374fce5edbc8e2a8697c15331677e6ebf0b";
    in = abi.abiIn("insert(string,string)", tableName, addr);
    callResult = authorityPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
    // query
    entries = table->select(precompiled::getTableName(tableName), table->newCondition());
    BOOST_TEST(entries->size() == 2u);
}

BOOST_AUTO_TEST_CASE(insert_overflow)
{
    // first insert
    protocol::ContractABICodec abi;
    std::string tableName = "66666012345678901234567890123456789012345678901234567890123456789";
    std::string addr = "0x420f853b49838bd3e9466c85a4cc3428c960dde2";
    bytes in = abi.abiIn("insert(string,string)", tableName, addr);
    auto callResult = authorityPrecompiled->call(context, bytesConstRef(&in));
    bytes out = callResult->execResult();
    s256 errCode;
    abi.abiOut(&out, errCode);
    BOOST_TEST(errCode == CODE_TABLE_NAME_OVERFLOW);
}

BOOST_AUTO_TEST_CASE(remove)
{
    // first insert
    protocol::ContractABICodec abi;
    std::string tableName = "t_test";
    std::string addr = "0x420f853b49838bd3e9466c85a4cc3428c960dde2";
    bytes in = abi.abiIn("insert(string,string)", tableName, addr);
    auto callResult = authorityPrecompiled->call(context, bytesConstRef(&in));
    bytes out = callResult->execResult();
    // query
    auto table = memoryTableFactory->openTable(SYS_ACCESS_TABLE);
    auto entries = table->select(precompiled::getTableName(tableName), table->newCondition());
    BOOST_TEST(entries->size() == 1u);

    // remove
    in = abi.abiIn("remove(string,string)", tableName, addr);
    callResult = authorityPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();

    // query
    table = memoryTableFactory->openTable(SYS_ACCESS_TABLE);
    Condition::Ptr condition = table->newCondition();
    condition->EQ(STATUS, "1");
    entries = table->select(precompiled::getTableName(tableName), condition);
    BOOST_TEST(entries->size() == 0u);

    // remove not exist entry
    tableName = "t_ok";
    in = abi.abiIn("remove(string,string)", tableName, addr);
    authorityPrecompiled->call(context, bytesConstRef(&in));
    BOOST_TEST(entries->size() == 0u);
}

BOOST_AUTO_TEST_CASE(queryByName)
{
    // insert
    protocol::ContractABICodec abi;
    std::string tableName = "t_test";
    std::string addr = "0x420f853b49838bd3e9466c85a4cc3428c960dde2";
    bytes in = abi.abiIn("insert(string,string)", tableName, addr);

    auto callResult = authorityPrecompiled->call(context, bytesConstRef(&in));
    bytes out = callResult->execResult();

    // queryByName by a existing key
    in = abi.abiIn("queryByName(string)", tableName);
    callResult = authorityPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
    std::string retStr;
    abi.abiOut(&out, retStr);

    Json::Value retJson;
    Json::Reader reader;
    BOOST_TEST(reader.parse(retStr, retJson) == true);
    BOOST_TEST(retJson.size() == 1);

    std::string keyName = "test";
    // queryByName by a no existing key
    in = abi.abiIn("queryByName(string)", keyName);
    callResult = authorityPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
    abi.abiOut(&out, retStr);
    BOOST_TEST(reader.parse(retStr, retJson) == true);
    BOOST_TEST(retJson.size() == 0);
}

BOOST_AUTO_TEST_CASE(error_func)
{
    // insert
    protocol::ContractABICodec abi;
    std::string tableName = "t_test";
    std::string addr = "0x420f853b49838bd3e9466c85a4cc3428c960dde2";
    bytes in = abi.abiIn("insert(string)", tableName, addr);
    auto callResult = authorityPrecompiled->call(context, bytesConstRef(&in));
    bytes out = callResult->execResult();
}

BOOST_AUTO_TEST_CASE(toString)
{
    BOOST_TEST(authorityPrecompiled->toString() == "Permission");
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_AuthorityPrecompiled
