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
/** @file test_ParallelConfigPrecompiled.cpp
 *  @author jimmyshi
 *  @date 20190315
 */
#include "Common.h"
#include "MemoryStorage.h"
#include <libblockverifier/ExecutiveContextFactory.h>
#include <libdevcrypto/Common.h>
#include <libethcore/ABI.h>
#include <libprecompiled/ParallelConfigPrecompiled.h>
#include <libstorage/MemoryTable.h>
#include <libstorage/MemoryTableFactoryFactory.h>
#include <libstoragestate/StorageState.h>
#include <libstoragestate/StorageStateFactory.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <iostream>
#include <string>

using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace dev::blockverifier;
using namespace dev::storage;
using namespace dev::storagestate;
using namespace dev::precompiled;

namespace dev
{
namespace test
{
class ParallelConfigPrecompiledFixture : TestOutputHelperFixture
{
public:
    const string PARA_KEY = "parallel";
    const string PARA_SELECTOR = "selector";
    const string PARA_FUNC_NAME = "functionName";
    const string PARA_CRITICAL_SIZE = "criticalSize";

    const string PARA_CONFIG_REGISTER_METHOD_ADDR_STR_UINT =
        "registerParallelFunctionInternal(address,string,uint256)";
    const string PARA_CONFIG_UNREGISTER_METHOD_ADDR_STR =
        "unregisterParallelFunctionInternal(address,string)";

public:
    ParallelConfigPrecompiledFixture() : TestOutputHelperFixture()
    {
        blockInfo.hash = h256(0);
        blockInfo.number = 0;
        context = std::make_shared<ExecutiveContext>();
        ExecutiveContextFactory factory(false);
        auto storage = std::make_shared<MemoryStorage>();
        auto storageStateFactory = std::make_shared<StorageStateFactory>(h256(0));
        factory.setStateStorage(storage);
        factory.setStateFactory(storageStateFactory);
        auto tableFactoryFactory = std::make_shared<MemoryTableFactoryFactory>();
        factory.setTableFactoryFactory(tableFactoryFactory);
        factory.initExecutiveContext(blockInfo, h256(0), context);
        memoryTableFactory = context->getMemoryTableFactory();
        parallelConfigPrecompiled = std::dynamic_pointer_cast<ParallelConfigPrecompiled>(
            context->getPrecompiled(Address(0x1006)));

        auto precompiledGasFactory = std::make_shared<dev::precompiled::PrecompiledGasFactory>(0);
        auto precompiledExecResultFactory =
            std::make_shared<dev::precompiled::PrecompiledExecResultFactory>();
        precompiledExecResultFactory->setPrecompiledGasFactory(precompiledGasFactory);
        parallelConfigPrecompiled->setPrecompiledExecResultFactory(precompiledExecResultFactory);
    };

    virtual ~ParallelConfigPrecompiledFixture(){};

    bytes callPrecompiled(bytesConstRef _param)
    {
        return (parallelConfigPrecompiled->call(context, _param, Address(0x12345)))->execResult();
    }

    bool hasRegistered(const Address& _address, const string& _functionName)
    {
        auto tableName = PARA_CONFIG_TABLE_PREFIX + _address.hex() + "_";
        if (g_BCOSConfig.version() >= V2_2_0)
        {
            tableName = PARA_CONFIG_TABLE_PREFIX_SHORT + _address.hex();
        }
        Table::Ptr table = memoryTableFactory->openTable(tableName);
        if (!table)
        {
            return false;
        }

        uint32_t func = parallelConfigPrecompiled->getFuncSelector(_functionName);
        Condition::Ptr cond = table->newCondition();
        cond->EQ(PARA_SELECTOR, to_string(func));
        auto entries = table->select(PARA_KEY, cond);

        return entries->size() != 0;
    }

    uint32_t getFuncSelector(const string& _functionName)
    {
        return parallelConfigPrecompiled->getFuncSelector(_functionName);
    }

public:
    ExecutiveContext::Ptr context;
    TableFactory::Ptr memoryTableFactory;
    BlockInfo blockInfo;
    ParallelConfigPrecompiled::Ptr parallelConfigPrecompiled;
};

BOOST_FIXTURE_TEST_SUITE(test_ParallelConfigPrecompiled, ParallelConfigPrecompiledFixture)

BOOST_AUTO_TEST_CASE(toString)
{
    BOOST_CHECK_EQUAL(parallelConfigPrecompiled->toString(), "ParallelConfig");
}

BOOST_AUTO_TEST_CASE(registerParallelFunction)
{
    Address contractAddr = Address(0x23333333);
    const string TRANSFER_FUNC = "transfer(string,string,uint256)";
    uint32_t selector = getFuncSelector(TRANSFER_FUNC);

    BOOST_CHECK(hasRegistered(contractAddr, TRANSFER_FUNC) == false);

    // insert
    ContractABI abi;
    bytes param =
        abi.abiIn(PARA_CONFIG_REGISTER_METHOD_ADDR_STR_UINT, contractAddr, TRANSFER_FUNC, 2);
    bytes ret = callPrecompiled(ref(param));
    BOOST_CHECK(ret == abi.abiIn("", (int)CODE_SUCCESS));
    BOOST_CHECK(hasRegistered(contractAddr, TRANSFER_FUNC) == true);

    auto config = parallelConfigPrecompiled->getParallelConfig(
        context, contractAddr, selector, Address(0x12345));
    BOOST_CHECK_EQUAL(config->functionName, TRANSFER_FUNC);
    BOOST_CHECK_EQUAL(config->criticalSize, u256(2));

    // update
    param = abi.abiIn(PARA_CONFIG_REGISTER_METHOD_ADDR_STR_UINT, contractAddr, TRANSFER_FUNC, 1);
    ret = callPrecompiled(ref(param));
    BOOST_CHECK(ret == abi.abiIn("", (int)CODE_SUCCESS));
    BOOST_CHECK(hasRegistered(contractAddr, TRANSFER_FUNC) == true);

    config = parallelConfigPrecompiled->getParallelConfig(
        context, contractAddr, selector, Address(0x12345));
    BOOST_CHECK_EQUAL(config->functionName, TRANSFER_FUNC);
    BOOST_CHECK_EQUAL(config->criticalSize, u256(1));

    // delete
    param = abi.abiIn(PARA_CONFIG_UNREGISTER_METHOD_ADDR_STR, contractAddr, TRANSFER_FUNC);
    ret = callPrecompiled(ref(param));
    BOOST_CHECK(ret == abi.abiIn("", (int)CODE_SUCCESS));
    BOOST_CHECK(hasRegistered(contractAddr, TRANSFER_FUNC) == false);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace dev
