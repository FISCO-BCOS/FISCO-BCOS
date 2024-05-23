#include "../mock/MockLedger.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/protocol/Exceptions.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-table/src/StateStorage.h"
#include "bcos-task/Wait.h"
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
        storageWrapper = std::make_shared<StorageWrapper>(m_blockContext.storage(), m_recoder);
        m_storageWrapper = storageWrapper.get();
    }
    std::shared_ptr<StorageWrapper> storageWrapper;
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
    std::shared_ptr<StateStorage> backendStorage = std::make_shared<StateStorage>(nullptr, false);
    std::shared_ptr<StateStorage> stateStorage =
        std::make_shared<StateStorage>(backendStorage, false);
    std::shared_ptr<BlockContext> blockContext = std::make_shared<BlockContext>(stateStorage,
        ledgerCache, hashImpl, 0, h256(), utcTime(),
        static_cast<uint32_t>(protocol::BlockVersion::V3_1_VERSION), false, false, backendStorage);
    std::shared_ptr<MockTransactionExecutive> executive =
        std::make_shared<MockTransactionExecutive>(*blockContext, "", 100, 0, *gasInjector);
};

BOOST_FIXTURE_TEST_SUITE(SystemConfigPrecompiledTest, SystemConfigPrecompiledFixture)

BOOST_AUTO_TEST_CASE(getAndSetFeature)
{
    SystemConfigPrecompiled systemConfigPrecompiled(hashImpl);
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


    setInput = codec.encodeWithSig("setValueByKey(string,string)",
        std::string("feature_balance_precompiled"), std::string("1"));
    setParameters->m_input = bcos::ref(setInput);
    BOOST_CHECK_THROW(systemConfigPrecompiled.call(executive, setParameters), PrecompiledError);

    setInput = codec.encodeWithSig(
        "setValueByKey(string,string)", std::string("feature_balance_policy1"), std::string("1"));
    setParameters->m_input = bcos::ref(setInput);
    BOOST_CHECK_THROW(systemConfigPrecompiled.call(executive, setParameters), PrecompiledError);

    setInput = codec.encodeWithSig(
        "setValueByKey(string,string)", std::string("feature_balance"), std::string("1"));
    setParameters->m_input = bcos::ref(setInput);
    auto featureBalanceResult = systemConfigPrecompiled.call(executive, setParameters);

    codec.decode(bcos::ref(featureBalanceResult->execResult()), code);
    BOOST_CHECK_EQUAL(code, 0);

    std::shared_ptr<LedgerCache> ledgerCache =
        std::make_shared<LedgerCache>(std::make_shared<bcos::test::MockLedger>());
    std::shared_ptr<BlockContext> newBlockContext = std::make_shared<BlockContext>(
        executive->blockContext().storage(), ledgerCache, executive->blockContext().hashHandler(),
        1, h256(), utcTime(), static_cast<uint32_t>(protocol::BlockVersion::V3_1_VERSION), false,
        false, backendStorage);
    std::shared_ptr<MockTransactionExecutive> newExecutive =
        std::make_shared<MockTransactionExecutive>(*newBlockContext, "", 100, 0, *gasInjector);
    setInput = codec.encodeWithSig("setValueByKey(string,string)",
        std::string("feature_balance_precompiled"), std::string("1"));
    setParameters->m_input = bcos::ref(setInput);
    auto featureBalancePreResult = systemConfigPrecompiled.call(newExecutive, setParameters);

    codec.decode(bcos::ref(featureBalancePreResult->execResult()), code);
    BOOST_CHECK_EQUAL(code, 0);

    setInput = codec.encodeWithSig(
        "setValueByKey(string,string)", std::string("feature_balance_policy1"), std::string("1"));
    setParameters->m_input = bcos::ref(setInput);
    BOOST_CHECK_THROW(systemConfigPrecompiled.call(newExecutive, setParameters), PrecompiledError);
}

BOOST_AUTO_TEST_CASE(upgradeVersion)
{
    task::syncWait([this]() -> task::Task<void> {
        SystemConfigPrecompiled systemConfigPrecompiled(hashImpl);
        auto setParameters = std::make_shared<PrecompiledExecResult>();

        CodecWrapper codec(hashImpl, false);
        auto setInput = codec.encodeWithSig("setValueByKey(string,string)",
            std::string(bcos::ledger::SYSTEM_KEY_COMPATIBILITY_VERSION), std::string("3.1.3"));
        setParameters->m_input = bcos::ref(setInput);
        auto result = systemConfigPrecompiled.call(executive, setParameters);
        bcos::s256 code = -1;
        codec.decode(bcos::ref(result->execResult()), code);
        BOOST_CHECK_EQUAL(code, 0);

        auto getRevertParameters = std::make_shared<PrecompiledExecResult>();
        auto getInput = codec.encodeWithSig("getValueByKey(string)", std::string("bugfix_revert"));
        getRevertParameters->m_input = bcos::ref(getInput);
        result = systemConfigPrecompiled.call(executive, getRevertParameters);
        std::string value;
        codec.decode(bcos::ref(result->execResult()), value);
        BOOST_CHECK_EQUAL(value, "");

        setInput = codec.encodeWithSig("setValueByKey(string,string)",
            std::string(bcos::ledger::SYSTEM_KEY_COMPATIBILITY_VERSION), std::string("3.2.3"));
        setParameters->m_input = bcos::ref(setInput);
        result = systemConfigPrecompiled.call(executive, setParameters);
        code = -1;
        codec.decode(bcos::ref(result->execResult()), code);
        BOOST_CHECK_EQUAL(code, 0);

        auto entry = co_await storage2::readOne(*backendStorage,
            transaction_executor::StateKeyView(ledger::SYS_CONFIG, "bugfix_revert"));
        BOOST_CHECK(!entry);

        result = systemConfigPrecompiled.call(executive, getRevertParameters);
        codec.decode(bcos::ref(result->execResult()), value);
        BOOST_CHECK_EQUAL(value, "1");

        // Check if set feature_sharding to backend storage
        setInput = codec.encodeWithSig("setValueByKey(string,string)",
            std::string(bcos::ledger::SYSTEM_KEY_COMPATIBILITY_VERSION), std::string("3.3.0"));
        setParameters->m_input = bcos::ref(setInput);
        result = systemConfigPrecompiled.call(executive, setParameters);
        code = -1;
        codec.decode(bcos::ref(result->execResult()), code);
        BOOST_CHECK_EQUAL(code, 0);

        entry = co_await storage2::readOne(*backendStorage,
            bcos::transaction_executor::StateKeyView(ledger::SYS_CONFIG, "feature_sharding"));
        BOOST_CHECK(entry);

        auto getShardingParameters = std::make_shared<PrecompiledExecResult>();
        auto getShardingInput =
            codec.encodeWithSig("getValueByKey(string)", std::string("feature_sharding"));
        getShardingParameters->m_input = bcos::ref(getInput);

        result = systemConfigPrecompiled.call(executive, getRevertParameters);
        codec.decode(bcos::ref(result->execResult()), value);
        BOOST_CHECK_EQUAL(value, "1");
    }());
}

BOOST_AUTO_TEST_SUITE_END()