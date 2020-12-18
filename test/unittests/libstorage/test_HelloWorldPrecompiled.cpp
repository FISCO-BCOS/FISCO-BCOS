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
 * @file test_HelloWorldPrecompiled.cpp
 * @author: octopuswang
 * @date 20181120
 */

#include "Common.h"
#include "MemoryStorage.h"
#include <libblockverifier/ExecutiveContextFactory.h>
#include <libdevcrypto/Common.h>
#include <libprecompiled/extension/HelloWorldPrecompiled.h>
#include <libprotocol/ABI.h>
#include <libstorage/MemoryTableFactoryFactory.h>
#include <libstoragestate/StorageStateFactory.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::blockverifier;
using namespace bcos::storage;
using namespace bcos::storagestate;
using namespace bcos::precompiled;

namespace test_HelloWorldPrecompiled
{
struct HelloWorldPrecompiledFixture
{
    HelloWorldPrecompiledFixture()
    {
        blockInfo.hash = h256(0);
        blockInfo.number = 0;
        context = std::make_shared<ExecutiveContext>();
        ExecutiveContextFactory factory;
        auto storage = std::make_shared<MemoryStorage>();
        auto storageStateFactory = std::make_shared<StorageStateFactory>(h256(0));
        factory.setStateStorage(storage);
        factory.setStateFactory(storageStateFactory);
        auto tableFactoryFactory = std::make_shared<MemoryTableFactoryFactory>();
        factory.setTableFactoryFactory(tableFactoryFactory);
        factory.initExecutiveContext(blockInfo, h256(0), context);
        helloWorldPrecompiled = std::make_shared<HelloWorldPrecompiled>();
        memoryTableFactory = context->getMemoryTableFactory();

        auto precompiledGasFactory = std::make_shared<bcos::precompiled::PrecompiledGasFactory>(0);
        auto precompiledExecResultFactory =
            std::make_shared<bcos::precompiled::PrecompiledExecResultFactory>();
        precompiledExecResultFactory->setPrecompiledGasFactory(precompiledGasFactory);
        helloWorldPrecompiled->setPrecompiledExecResultFactory(precompiledExecResultFactory);
    }

    ~HelloWorldPrecompiledFixture() {}

    ExecutiveContext::Ptr context;
    TableFactory::Ptr memoryTableFactory;
    HelloWorldPrecompiled::Ptr helloWorldPrecompiled;
    BlockInfo blockInfo;

    std::string setFunc{"set(string)"};
    std::string getFunc{"get()"};
};

BOOST_FIXTURE_TEST_SUITE(test_HelloWorldPrecompiled, HelloWorldPrecompiledFixture)

BOOST_AUTO_TEST_CASE(toString)
{
    BOOST_TEST(helloWorldPrecompiled->toString() == "HelloWorld");
}

BOOST_AUTO_TEST_CASE(get)
{  // function get() public constant returns(string);
    bcos::protocol::ContractABI abi;
    bytes out;

    std::string defaultValue = "Hello World!";
    std::string strValue;

    // original state, default value should return
    bytes params = abi.abiIn(getFunc);
    auto callResult = helloWorldPrecompiled->call(context, bytesConstRef(&params));
    out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), strValue);
    BOOST_TEST(defaultValue == strValue);
}

BOOST_AUTO_TEST_CASE(set)
{  // function get() public constant returns(string);
    bcos::protocol::ContractABI abi;
    bytes out;
    bytes params;

    std::string defaultValue = "Hello World!";

    // set value to empty string
    std::string strSetValue0 = "";
    std::string strGetValue0 = defaultValue;

    params = abi.abiIn(setFunc, strSetValue0);
    auto callResult = helloWorldPrecompiled->call(context, bytesConstRef(&params));
    out = callResult->execResult();
    // get it , empty string should return
    params = abi.abiIn(getFunc);
    callResult = helloWorldPrecompiled->call(context, bytesConstRef(&params));
    out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), strGetValue0);
    BOOST_TEST(strGetValue0 == strSetValue0);


    // set value to "aaaaaaaaaaaaaaaaaaaa"
    std::string strSetValue1 = "aaaaaaaaaaaaaaaaaaaa";
    std::string strGetValue1;

    params = abi.abiIn(setFunc, strSetValue1);
    callResult = helloWorldPrecompiled->call(context, bytesConstRef(&params));
    out = callResult->execResult();
    // get it , "aaaaaaaaaaaaaaaaaaaa" string should return
    params = abi.abiIn(getFunc);
    callResult = helloWorldPrecompiled->call(context, bytesConstRef(&params));
    out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), strGetValue1);
    BOOST_TEST(strGetValue1 == strSetValue1);

    // set value to "bbbbbbbbbb"
    std::string strSetValue2 = "bbbbbbbbbb";
    std::string strGetValue2;

    params = abi.abiIn(setFunc, strSetValue2);
    callResult = helloWorldPrecompiled->call(context, bytesConstRef(&params));
    out = callResult->execResult();
    // get it , "aaaaaaaaaaaaaaaaaaaa" string should return
    params = abi.abiIn(getFunc);
    callResult = helloWorldPrecompiled->call(context, bytesConstRef(&params));
    out = callResult->execResult();
    abi.abiOut(bytesConstRef(&out), strGetValue2);
    BOOST_TEST(strGetValue2 == strSetValue2);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_HelloWorldPrecompiled
