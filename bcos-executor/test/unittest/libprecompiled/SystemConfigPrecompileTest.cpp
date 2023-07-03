#include "../mock/MockLedger.h"
#include "bcos-table/src/StateStorage.h"
#include "executor/TransactionExecutor.h"
#include "libprecompiled/PreCompiledFixture.h"
#include "precompiled/SystemConfigPrecompiled.h"
#include "precompiled/TableManagerPrecompiled.h"
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
        m_storageWrapper =
            std::make_shared<StorageWrapper>(m_blockContext.lock()->storage(), m_recoder);
    }
};

struct SystemConfigPrecompiledFixture : public bcos::test::PrecompiledFixture
{
    SystemConfigPrecompiledFixture()
    {
        GlobalHashImpl::g_hashImpl = hashImpl;
        systemConfigPrecompiled.emplace();
        executive->setStorageWrapper();
    }

    std::shared_ptr<bcos::crypto::Keccak256> hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    std::shared_ptr<LedgerCache> ledgerCache =
        std::make_shared<LedgerCache>(std::make_shared<bcos::test::MockLedger>());
    std::shared_ptr<wasm::GasInjector> gasInjector;
    std::shared_ptr<StateStorage> stateStorage = std::make_shared<StateStorage>(nullptr);
    std::shared_ptr<BlockContext> blockContext = std::make_shared<BlockContext>(stateStorage,
        ledgerCache, hashImpl, 0, h256(), utcTime(), 0, FiscoBcosSchedule, false, false);
    std::shared_ptr<MockTransactionExecutive> executive =
        std::make_shared<MockTransactionExecutive>(
            std::weak_ptr<BlockContext>(blockContext), "", 100, 0, gasInjector);

    std::optional<SystemConfigPrecompiled> systemConfigPrecompiled;
};

BOOST_FIXTURE_TEST_SUITE(SystemConfigPrecompiledTest, SystemConfigPrecompiledFixture)

BOOST_AUTO_TEST_CASE(getXConfig)
{
    auto parameters = std::make_shared<PrecompiledExecResult>();

    CodecWrapper codec(hashImpl, false);
    auto input = codec.encodeWithSig(
        std::string("setValueByKey(string,string)"), std::string("x_arg1"), std::string("100"));
    parameters->m_input = bcos::ref(input);
    auto result = systemConfigPrecompiled->call(executive, parameters);

    s256 code = -1;
    codec.decode(bcos::ref(result->execResult()), code);
    BOOST_CHECK_EQUAL(code, 0);
}

BOOST_AUTO_TEST_SUITE_END()