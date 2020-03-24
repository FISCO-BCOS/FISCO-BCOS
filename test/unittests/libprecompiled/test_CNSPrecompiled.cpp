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
 * @brief: unit test for CNS
 *
 * @file test_CNSPrecompiled.cpp
 * @author: chaychen
 * @date 20181120
 */

#include "../libstorage/MemoryStorage.h"
#include <json/json.h>
#include <libblockverifier/ExecutiveContextFactory.h>
#include <libdevcrypto/Common.h>
#include <libethcore/ABI.h>
#include <libprecompiled/CNSPrecompiled.h>
#include <libstorage/MemoryTable.h>
#include <libstorage/MemoryTableFactoryFactory2.h>
#include <libstoragestate/StorageStateFactory.h>
#include <boost/test/unit_test.hpp>

using namespace std;
using namespace dev;
using namespace dev::blockverifier;
using namespace dev::storage;
using namespace dev::storagestate;
using namespace dev::precompiled;

namespace test_CNSPrecompiled
{
struct CNSPrecompiledFixture
{
    CNSPrecompiledFixture()
    {
        blockInfo.hash = h256(0);
        blockInfo.number = 0;
        context = std::make_shared<ExecutiveContext>();
        ExecutiveContextFactory factory;
        auto storage = std::make_shared<MemoryStorage>();
        auto storageStateFactory = std::make_shared<StorageStateFactory>(h256(0));
        auto tableFactoryFactory = std::make_shared<MemoryTableFactoryFactory2>();
        factory.setStateStorage(storage);
        factory.setStateFactory(storageStateFactory);
        factory.setTableFactoryFactory(tableFactoryFactory);
        factory.initExecutiveContext(blockInfo, h256(0), context);
        cnsPrecompiled = std::make_shared<CNSPrecompiled>();
        auto precompiledExecResultFactory = std::make_shared<PrecompiledExecResultFactory>();
        cnsPrecompiled->setPrecompiledExecResultFactory(precompiledExecResultFactory);
        memoryTableFactory = context->getMemoryTableFactory();
    }

    ~CNSPrecompiledFixture() {}

    ExecutiveContext::Ptr context;
    TableFactory::Ptr memoryTableFactory;
    CNSPrecompiled::Ptr cnsPrecompiled;
    BlockInfo blockInfo;
};

BOOST_FIXTURE_TEST_SUITE(test_CNSPrecompiled, CNSPrecompiledFixture)

BOOST_AUTO_TEST_CASE(insert)
{
    // first insert
    eth::ContractABI abi;
    std::string contractName = "Ok";
    std::string contractVersion = "1.0";
    std::string contractAddress = "0x420f853b49838bd3e9466c85a4cc3428c960dde2";
    std::string contractAbi =
        "[{\"constant\":false,\"inputs\":[{\"name\":\"num\",\"type\":\"uint256\"}],\"name\":"
        "\"trans\",\"outputs\":[],\"payable\":false,\"type\":\"function\"},{\"constant\":true,"
        "\"inputs\":[],\"name\":\"get\",\"outputs\":[{\"name\":\"\",\"type\":\"uint256\"}],"
        "\"payable\":false,\"type\":\"function\"},{\"inputs\":[],\"payable\":false,\"type\":"
        "\"constructor\"}]";
    // insert overflow
    string overflowVersion130 =
        "012345678901234567890123456789012345678901234567890123456789"
        "0123456789012345678901234567890123456789012345678901234567890123456789";
    bytes in2 = abi.abiIn("insert(string,string,string,string)", contractName, overflowVersion130,
        contractAddress, contractAbi);
    auto callResult = (cnsPrecompiled->call(context, bytesConstRef(&in2)));
    bytes out2 = callResult->execResult();
    s256 errCode;
    abi.abiOut(&out2, errCode);
    BOOST_TEST(errCode == CODE_VERSION_LENGTH_OVERFLOW);

    // insert
    bytes in = abi.abiIn("insert(string,string,string,string)", contractName, contractVersion,
        contractAddress, contractAbi);
    callResult = cnsPrecompiled->call(context, bytesConstRef(&in));
    bytes out = callResult->execResult();
    // query
    auto table = memoryTableFactory->openTable(SYS_CNS);
    auto entries = table->select(contractName, table->newCondition());
    BOOST_TEST(entries->size() == 1u);


    // insert again with same item
    callResult = cnsPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
    // query
    table = memoryTableFactory->openTable(SYS_CNS);
    entries = table->select(contractName, table->newCondition());
    BOOST_TEST(entries->size() == 1u);

    // insert new item with same name, address and abi
    contractVersion = "2.0";
    in = abi.abiIn("insert(string,string,string,string)", contractName, contractVersion,
        contractAddress, contractAbi);
    callResult = cnsPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
    // query
    table = memoryTableFactory->openTable(SYS_CNS);
    entries = table->select(contractName, table->newCondition());
    BOOST_TEST(entries->size() == 2u);
}

BOOST_AUTO_TEST_CASE(select)
{
    // first insert
    eth::ContractABI abi;
    std::string contractName = "Ok";
    std::string contractVersion = "1.0";
    std::string contractAddress = "0x420f853b49838bd3e9466c85a4cc3428c960dde2";
    std::string contractAbi =
        "[{\"constant\":false,\"inputs\":[{\"name\":\"num\",\"type\":\"uint256\"}],\"name\":"
        "\"trans\",\"outputs\":[],\"payable\":false,\"type\":\"function\"},{\"constant\":true,"
        "\"inputs\":[],\"name\":\"get\",\"outputs\":[{\"name\":\"\",\"type\":\"uint256\"}],"
        "\"payable\":false,\"type\":\"function\"},{\"inputs\":[],\"payable\":false,\"type\":"
        "\"constructor\"}]";
    bytes in = abi.abiIn("insert(string,string,string,string)", contractName, contractVersion,
        contractAddress, contractAbi);
    auto callResult = cnsPrecompiled->call(context, bytesConstRef(&in));
    bytes out = callResult->execResult();
    // insert new item with same name, address and abi
    contractVersion = "2.0";
    in = abi.abiIn("insert(string,string,string,string)", contractName, contractVersion,
        contractAddress, contractAbi);
    callResult = cnsPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();

    // select existing keys
    in = abi.abiIn("selectByName(string)", contractName);
    callResult = cnsPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
    std::string retStr;
    abi.abiOut(&out, retStr);

    LOG(TRACE) << "select result:" << retStr;
    Json::Value retJson;
    Json::Reader reader;
    BOOST_TEST(reader.parse(retStr, retJson) == true);
    BOOST_TEST(retJson.size() == 2);

    // getContractAddress
    in = abi.abiIn("getContractAddress(string,string)", contractName, contractVersion);
    callResult = cnsPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
    Address ret;
    abi.abiOut(&out, ret);
    BOOST_TEST(ret == Address(contractAddress));

    // select no existing keys
    in = abi.abiIn("selectByName(string)", std::string("Ok2"));
    callResult = cnsPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
    abi.abiOut(&out, retStr);
    LOG(TRACE) << "select result:" << retStr;
    BOOST_TEST(reader.parse(retStr, retJson) == true);
    BOOST_TEST(retJson.size() == 0);

    // select existing keys and version
    in = abi.abiIn("selectByNameAndVersion(string,string)", contractName, contractVersion);
    callResult = cnsPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();

    abi.abiOut(&out, retStr);
    LOG(TRACE) << "select result:" << retStr;
    BOOST_TEST(reader.parse(retStr, retJson) == true);
    BOOST_TEST(retJson.size() == 1);

    // select no existing keys and version
    in = abi.abiIn("selectByNameAndVersion(string,string)", contractName, std::string("3.0"));
    callResult = cnsPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
    abi.abiOut(&out, retStr);
    LOG(TRACE) << "select result:" << retStr;
    BOOST_TEST(reader.parse(retStr, retJson) == true);
    BOOST_TEST(retJson.size() == 0);
    in = abi.abiIn("selectByNameAndVersion(string,string)", std::string("Ok2"), contractVersion);
    callResult = cnsPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
    abi.abiOut(&out, retStr);
    LOG(TRACE) << "select result:" << retStr;
    BOOST_TEST(reader.parse(retStr, retJson) == true);
    BOOST_TEST(retJson.size() == 0);
}

BOOST_AUTO_TEST_CASE(toString)
{
    BOOST_TEST(cnsPrecompiled->toString() == "CNS");
}

BOOST_AUTO_TEST_CASE(errFunc)
{
    eth::ContractABI abi;
    bytes in = abi.abiIn("insert(string)", std::string("test"));
    auto callResult = cnsPrecompiled->call(context, bytesConstRef(&in));
    bytes out = callResult->execResult();
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_CNSPrecompiled
