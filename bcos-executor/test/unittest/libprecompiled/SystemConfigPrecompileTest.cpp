#include "../mock/MockLedger.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/protocol/Exceptions.h"
#include "bcos-table/src/StateStorage.h"
#include "executor/TransactionExecutor.h"
#include "libprecompiled/PreCompiledFixture.h"
#include "precompiled/SystemConfigPrecompiled.h"
#include "precompiled/TableManagerPrecompiled.h"
#include "precompiled/common/PrecompiledResult.h"
#include <bcos-utilities/testutils/TestPromptFixture.h>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::ledger;

class MockTransactionExecutive : public TransactionExecutive
{
public:
    using TransactionExecutive::TransactionExecutive;
    void setStorageWrapper()
    {
        m_storageWrapper = std::make_shared<StorageWrapper>(m_blockContext.storage(), m_recoder);
    }
};

struct SystemConfigPrecompiledFixture : public bcos::test::PrecompiledFixture
{
    SystemConfigPrecompiledFixture()
    {
        GlobalHashImpl::g_hashImpl = hashImpl;
        executive->setStorageWrapper();

        stateStorage->createTable(std::string(SYS_CONFIG), SYS_VALUE_AND_ENABLE_BLOCK_NUMBER);
    }

    std::shared_ptr<bcos::crypto::Keccak256> hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    std::shared_ptr<LedgerCache> ledgerCache =
        std::make_shared<LedgerCache>(std::make_shared<bcos::test::MockLedger>());
    std::shared_ptr<wasm::GasInjector> gasInjector;
    std::shared_ptr<StateStorage> stateStorage = std::make_shared<StateStorage>(nullptr);
    std::shared_ptr<BlockContext> blockContext =
        std::make_shared<BlockContext>(stateStorage, ledgerCache, hashImpl, 0, h256(), utcTime(),
            static_cast<uint32_t>(protocol::BlockVersion::V3_1_VERSION), FiscoBcosSchedule, false,
            false);
    std::shared_ptr<MockTransactionExecutive> executive =
        std::make_shared<MockTransactionExecutive>(*blockContext, "", 100, 0, *gasInjector);
};

BOOST_FIXTURE_TEST_SUITE(SystemConfigPrecompiledTest, SystemConfigPrecompiledFixture)

BOOST_AUTO_TEST_CASE(getAndSetFeature)
{
    SystemConfigPrecompiled systemConfigPrecompiled;
    auto setParameters = std::make_shared<PrecompiledExecResult>();

    CodecWrapper codec(hashImpl, false);
    auto setInput = codec.encodeWithSig(
        "setValueByKey(string,string)", std::string("feature_unknown"), std::string("100"));
    setParameters->m_input = bcos::ref(setInput);
    BOOST_CHECK_THROW(systemConfigPrecompiled.call(executive, setParameters), PrecompiledError);

    setInput = codec.encodeWithSig(
        "setValueByKey(string,string)", std::string("bugfix_revert"), std::string("2"));
    setParameters->m_input = bcos::ref(setInput);
    BOOST_CHECK_THROW(systemConfigPrecompiled.call(executive, setParameters), PrecompiledError);

    setInput = codec.encodeWithSig(
        "setValueByKey(string,string)", std::string("bugfix_revert"), std::string("1"));
    setParameters->m_input = bcos::ref(setInput);
    auto result = systemConfigPrecompiled.call(executive, setParameters);

    bcos::s256 code = -1;
    codec.decode(bcos::ref(result->execResult()), code);
    BOOST_CHECK_EQUAL(code, 0);

    auto getParameters = std::make_shared<PrecompiledExecResult>();
    auto getInput = codec.encodeWithSig("getValueByKey(string)", std::string("bugfix_revert"));
    getParameters->m_input = bcos::ref(getInput);
    result = systemConfigPrecompiled.call(executive, getParameters);

    std::string value;
    codec.decode(bcos::ref(result->execResult()), value);
    BOOST_CHECK_EQUAL(value, "1");
}

BOOST_AUTO_TEST_CASE(upgradeVersion)
{
    SystemConfigPrecompiled systemConfigPrecompiled;
    auto setParameters = std::make_shared<PrecompiledExecResult>();

    CodecWrapper codec(hashImpl, false);
    auto setInput = codec.encodeWithSig("setValueByKey(string,string)",
        std::string(bcos::ledger::SYSTEM_KEY_COMPATIBILITY_VERSION), std::string("3.1.3"));
    setParameters->m_input = bcos::ref(setInput);
    auto result = systemConfigPrecompiled.call(executive, setParameters);
    bcos::s256 code = -1;
    codec.decode(bcos::ref(result->execResult()), code);
    BOOST_CHECK_EQUAL(code, 0);

    auto getParameters = std::make_shared<PrecompiledExecResult>();
    auto getInput = codec.encodeWithSig("getValueByKey(string)", std::string("bugfix_revert"));
    getParameters->m_input = bcos::ref(getInput);
    result = systemConfigPrecompiled.call(executive, getParameters);
    std::string value;
    codec.decode(bcos::ref(result->execResult()), value);
    BOOST_CHECK_EQUAL(value, "");

    setInput = codec.encodeWithSig("setValueByKey(string,string)",
        std::string(bcos::ledger::SYSTEM_KEY_COMPATIBILITY_VERSION), std::string("3.2.0"));
    setParameters->m_input = bcos::ref(setInput);
    result = systemConfigPrecompiled.call(executive, setParameters);
    code = -1;
    codec.decode(bcos::ref(result->execResult()), code);
    BOOST_CHECK_EQUAL(code, 0);

    result = systemConfigPrecompiled.call(executive, getParameters);
    codec.decode(bcos::ref(result->execResult()), value);
    BOOST_CHECK_EQUAL(value, "1");
}

BOOST_AUTO_TEST_SUITE_END()