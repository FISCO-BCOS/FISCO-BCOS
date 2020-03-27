// "Copyright [2018] <fisco-dev>"
#include "Common.h"
#include "MemoryStorage.h"
#include "libstoragestate/StorageStateFactory.h"
#include <libblockverifier/ExecutiveContextFactory.h>
#include <libdevcrypto/Common.h>
#include <libethcore/ABI.h>
#include <libprecompiled/SystemConfigPrecompiled.h>
#include <libstorage/MemoryTable.h>
#include <libstorage/MemoryTableFactoryFactory2.h>
#include <boost/lexical_cast.hpp>
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace dev::blockverifier;
using namespace dev::storage;
using namespace dev::storagestate;
using namespace dev::precompiled;

namespace test_SystemConfigPrecompiled
{
struct SystemConfigPrecompiledFixture
{
    SystemConfigPrecompiledFixture()
    {
        blockInfo.hash = h256(0);
        blockInfo.number = 0;
        context = std::make_shared<ExecutiveContext>();
        ExecutiveContextFactory factory;
        auto storage = std::make_shared<MemoryStorage>();
        auto storageStateFactory = std::make_shared<StorageStateFactory>(h256(0));
        factory.setStateStorage(storage);
        factory.setStateFactory(storageStateFactory);
        auto tableFactoryFactory = std::make_shared<MemoryTableFactoryFactory2>();
        factory.setTableFactoryFactory(tableFactoryFactory);
        factory.initExecutiveContext(blockInfo, h256(0), context);
        systemConfigPrecompiled = std::make_shared<SystemConfigPrecompiled>();
        memoryTableFactory = context->getMemoryTableFactory();

        auto precompiledGasFactory = std::make_shared<dev::precompiled::PrecompiledGasFactory>(0);
        auto precompiledExecResultFactory =
            std::make_shared<dev::precompiled::PrecompiledExecResultFactory>();
        precompiledExecResultFactory->setPrecompiledGasFactory(precompiledGasFactory);
        systemConfigPrecompiled->setPrecompiledExecResultFactory(precompiledExecResultFactory);
    }

    ~SystemConfigPrecompiledFixture() {}

    ExecutiveContext::Ptr context;
    TableFactory::Ptr memoryTableFactory;
    SystemConfigPrecompiled::Ptr systemConfigPrecompiled;
    BlockInfo blockInfo;
};

BOOST_FIXTURE_TEST_SUITE(SystemConfigPrecompiled, SystemConfigPrecompiledFixture)

BOOST_AUTO_TEST_CASE(TestAddConfig)
{
    eth::ContractABI abi;

    LOG(INFO) << "Add a config key-value";
    std::string key1 = "tx_count_limit";
    uint64_t value1 = 10000000;
    bytes in =
        abi.abiIn("setValueByKey(string,string)", key1, boost::lexical_cast<std::string>(value1));
    auto callResult = systemConfigPrecompiled->call(context, bytesConstRef(&in));
    bytes out = callResult->execResult();
    s256 count = 0;
    abi.abiOut(bytesConstRef(&out), count);
    BOOST_TEST(count == 1u);

    LOG(INFO) << "Check the inserted key-value";
    auto table = memoryTableFactory->openTable(SYS_CONFIG);
    auto condition = table->newCondition();
    auto entries = table->select(key1, condition);
    BOOST_TEST(entries->size() == 1u);
    std::string ret = entries->get(0)->getField(SYSTEM_CONFIG_VALUE);
    BOOST_TEST(boost::lexical_cast<uint64_t>(ret) == value1);

    LOG(INFO) << "update the inserted key-value";
    uint64_t value2 = 20000000;
    in = abi.abiIn("setValueByKey(string,string)", key1, boost::lexical_cast<std::string>(value2));
    callResult = systemConfigPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
    count = 0;
    abi.abiOut(bytesConstRef(&out), count);
    BOOST_TEST(count == 1u);

    LOG(INFO) << "Check the new inserted key-value";
    table = memoryTableFactory->openTable(SYS_CONFIG);
    entries = table->select(key1, condition);
    BOOST_TEST(entries->size() == 1u);
    ret = entries->get(0)->getField(SYSTEM_CONFIG_VALUE);
    BOOST_TEST(boost::lexical_cast<uint64_t>(ret) == value2);
}

BOOST_AUTO_TEST_CASE(errFunc)
{
    eth::ContractABI abi;
    bytes in = abi.abiIn("insert(string)", std::string("test"));
    auto callResult = systemConfigPrecompiled->call(context, bytesConstRef(&in));
    bytes out = callResult->execResult();
}

BOOST_AUTO_TEST_CASE(InvalidValue)
{
    eth::ContractABI abi;
    bytes in =
        abi.abiIn("setValueByKey(string,string)", std::string("tx_count_limit"), std::string("0"));
    auto callResult = systemConfigPrecompiled->call(context, bytesConstRef(&in));
    bytes out = callResult->execResult();
    s256 count = 1;
    abi.abiOut(bytesConstRef(&out), count);
    if (g_BCOSConfig.version() > RC2_VERSION)
    {
        BOOST_TEST(count == CODE_INVALID_CONFIGURATION_VALUES);
    }
    else
    {
        BOOST_TEST(count == -CODE_INVALID_CONFIGURATION_VALUES);
    }

    in = abi.abiIn("setValueByKey(string,string)", std::string("tx_count_limit"), std::string("0"));
    callResult = systemConfigPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
    count = 1;
    abi.abiOut(bytesConstRef(&out), count);
    if (g_BCOSConfig.version() > RC2_VERSION)
    {
        BOOST_TEST(count == CODE_INVALID_CONFIGURATION_VALUES);
    }
    else
    {
        BOOST_TEST(count == -CODE_INVALID_CONFIGURATION_VALUES);
    }
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_SystemConfigPrecompiled
