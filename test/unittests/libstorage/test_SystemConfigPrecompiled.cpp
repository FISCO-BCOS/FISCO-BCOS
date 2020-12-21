// "Copyright [2018] <fisco-dev>"
#include "Common.h"
#include "MemoryStorage.h"
#include "libstoragestate/StorageStateFactory.h"
#include <libblockverifier/ExecutiveContextFactory.h>
#include <libdevcrypto/Common.h>
#include <libprecompiled/SystemConfigPrecompiled.h>
#include <libprotocol/ContractABICodec.h>
#include <libstorage/MemoryTableFactoryFactory.h>
#include <boost/lexical_cast.hpp>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::blockverifier;
using namespace bcos::storage;
using namespace bcos::storagestate;
using namespace bcos::precompiled;

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
        auto tableFactoryFactory = std::make_shared<MemoryTableFactoryFactory>();
        factory.setTableFactoryFactory(tableFactoryFactory);
        factory.initExecutiveContext(blockInfo, h256(0), context);
        systemConfigPrecompiled = std::make_shared<SystemConfigPrecompiled>();
        memoryTableFactory = context->getMemoryTableFactory();

        auto precompiledGasFactory = std::make_shared<bcos::precompiled::PrecompiledGasFactory>(0);
        auto precompiledExecResultFactory =
            std::make_shared<bcos::precompiled::PrecompiledExecResultFactory>();
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

void checkConfig(TableFactory::Ptr _memoryTableFactory, std::string const& _key,
    std::string const& _expectedValue)
{
    LOG(INFO) << "Check the inserted key-value";
    auto table = _memoryTableFactory->openTable(SYS_CONFIG);
    auto condition = table->newCondition();
    auto entries = table->select(_key, condition);
    BOOST_TEST(entries->size() == 1u);
    std::string ret = entries->get(0)->getField(SYSTEM_CONFIG_VALUE);
    BOOST_TEST(ret == _expectedValue);
}

void updateValue(bcos::precompiled::SystemConfigPrecompiled::Ptr _precompiled,
    ExecutiveContext::Ptr _context, std::string const& _key, std::string const& _value)
{
    protocol::ContractABICodec abi;

    LOG(INFO) << "Add a config key-value";
    bytes in = abi.abiIn("setValueByKey(string,string)", _key, _value);
    auto callResult = _precompiled->call(_context, bytesConstRef(&in));
    bytes out = callResult->execResult();
    s256 count = 0;
    abi.abiOut(bytesConstRef(&out), count);
    BOOST_TEST(count == 1u);
}

BOOST_AUTO_TEST_CASE(TestAddConfig)
{
    // test tx_count_limit
    uint64_t value = 10000000;
    std::string valueStr = boost::lexical_cast<std::string>(value);
    updateValue(systemConfigPrecompiled, context, "tx_count_limit", valueStr);
    checkConfig(memoryTableFactory, "tx_count_limit", valueStr);

    LOG(INFO) << "update the inserted key-value";
    uint64_t value2 = 20000000;
    valueStr = boost::lexical_cast<std::string>(value2);
    updateValue(systemConfigPrecompiled, context, "tx_count_limit", valueStr);

    LOG(INFO) << "Check the new inserted key-value";
    checkConfig(memoryTableFactory, "tx_count_limit", valueStr);

    // test tx_gas_limit
    valueStr = boost::lexical_cast<std::string>(1000000);
    updateValue(systemConfigPrecompiled, context, "tx_gas_limit", valueStr);
    checkConfig(memoryTableFactory, "tx_gas_limit", valueStr);

    // test epoch_block_num
    valueStr = boost::lexical_cast<std::string>(2);
    updateValue(systemConfigPrecompiled, context, SYSTEM_KEY_RPBFT_EPOCH_BLOCK_NUM, valueStr);
    checkConfig(memoryTableFactory, SYSTEM_KEY_RPBFT_EPOCH_BLOCK_NUM, valueStr);

    // test epoch_sealer_num
    valueStr = boost::lexical_cast<std::string>(10);
    updateValue(systemConfigPrecompiled, context, SYSTEM_KEY_RPBFT_EPOCH_SEALER_NUM, valueStr);
    checkConfig(memoryTableFactory, SYSTEM_KEY_RPBFT_EPOCH_SEALER_NUM, valueStr);
}

BOOST_AUTO_TEST_CASE(errFunc)
{
    protocol::ContractABICodec abi;
    bytes in = abi.abiIn("insert(string)", std::string("test"));
    auto callResult = systemConfigPrecompiled->call(context, bytesConstRef(&in));
    bytes out = callResult->execResult();
}

BOOST_AUTO_TEST_CASE(InvalidValue)
{
    protocol::ContractABICodec abi;
    bytes in =
        abi.abiIn("setValueByKey(string,string)", std::string("tx_count_limit"), std::string("0"));
    auto callResult = systemConfigPrecompiled->call(context, bytesConstRef(&in));
    bytes out = callResult->execResult();
    s256 count = 1;
    abi.abiOut(bytesConstRef(&out), count);
    BOOST_TEST(count == CODE_INVALID_CONFIGURATION_VALUES);

    in = abi.abiIn("setValueByKey(string,string)", std::string("tx_count_limit"), std::string("0"));
    callResult = systemConfigPrecompiled->call(context, bytesConstRef(&in));
    out = callResult->execResult();
    count = 1;
    abi.abiOut(bytesConstRef(&out), count);
    BOOST_TEST(count == CODE_INVALID_CONFIGURATION_VALUES);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_SystemConfigPrecompiled
