#include "../mock/MockLedger.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/ledger/SystemConfigs.h"
#include "bcos-framework/protocol/Exceptions.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-table/src/StateStorage.h"
#include "bcos-task/Wait.h"
#include "libprecompiled/PreCompiledFixture.h"
#include "precompiled/SystemConfigPrecompiled.h"
#include "precompiled/common/PrecompiledResult.h"
#include <boost/test/unit_test.hpp>
#include <magic_enum/magic_enum.hpp>

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
    std::shared_ptr<StateStorage> backendStorage = std::make_shared<StateStorage>(nullptr, false);
    std::shared_ptr<StateStorage> stateStorage =
        std::make_shared<StateStorage>(backendStorage, false);
    std::shared_ptr<BlockContext> blockContext =
        std::make_shared<BlockContext>(stateStorage, ledgerCache, hashImpl, 0, h256(), utcTime(),
            static_cast<uint32_t>(protocol::BlockVersion::V3_1_VERSION), false, backendStorage);
    std::shared_ptr<MockTransactionExecutive> executive =
        std::make_shared<MockTransactionExecutive>(*blockContext, "", 100, 0);
};

BOOST_FIXTURE_TEST_SUITE(SystemConfigPrecompiledTest, SystemConfigPrecompiledFixture)

BOOST_AUTO_TEST_CASE(getAndSetFeature)
{
    SystemConfigPrecompiled systemConfigPrecompiled(hashImpl);
    auto setParameters = std::make_shared<PrecompiledExecResult>();

    CodecWrapper codec(hashImpl);
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
    std::shared_ptr<BlockContext> newBlockContext =
        std::make_shared<BlockContext>(executive->blockContext().storage(), ledgerCache,
            executive->blockContext().hashHandler(), 1, h256(), utcTime(),
            static_cast<uint32_t>(protocol::BlockVersion::V3_1_VERSION), false, backendStorage);
    std::shared_ptr<MockTransactionExecutive> newExecutive =
        std::make_shared<MockTransactionExecutive>(*newBlockContext, "", 100, 0);
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

BOOST_AUTO_TEST_CASE(executorVersionOpstackSlotIsGenesisOnly)
{
    // OP mode is genesis-only (validateOpModeGenesisOnly): the genesis row is written by
    // Ledger::buildGenesisBlock with activation 0, never through this precompile. A
    // transaction that set the OPSTACK slot would land activation N+1 and make every node's
    // next start fail closed. Refused from this release on, and older blocks that could set
    // it must still replay, hence the version gate.
    SystemConfigPrecompiled systemConfigPrecompiled(hashImpl);
    auto setParameters = std::make_shared<PrecompiledExecResult>();
    CodecWrapper codec(hashImpl);
    auto const key =
        std::string(magic_enum::enum_name(bcos::ledger::SystemConfig::executor_version));
    auto const opstack = std::to_string(bcos::ledger::OPSTACK_EXECUTOR_VERSION);
    auto const callOn = [&](uint32_t blockVersion) {
        std::shared_ptr<BlockContext> blockContext =
            std::make_shared<BlockContext>(executive->blockContext().storage(), ledgerCache,
                executive->blockContext().hashHandler(), 1, h256(), utcTime(), blockVersion, false,
                backendStorage);
        auto executiveForVersion =
            std::make_shared<MockTransactionExecutive>(*blockContext, "", 100, 0);
        auto input = codec.encodeWithSig("setValueByKey(string,string)", key, opstack);
        setParameters->m_input = bcos::ref(input);
        return systemConfigPrecompiled.call(executiveForVersion, setParameters);
    };

    // From this release on the OPSTACK slot cannot be written by a transaction.
    BOOST_CHECK_THROW(
        callOn(static_cast<uint32_t>(protocol::BlockVersion::V3_18_0_VERSION)), PrecompiledError);
    // A pre-3.18 block that did set it still replays: the refusal is version-gated.
    BOOST_CHECK_NO_THROW(callOn(static_cast<uint32_t>(protocol::BlockVersion::V3_17_0_VERSION)));
}

BOOST_AUTO_TEST_CASE(executorVersionAboveTheDefinedLadderIsRefused)
{
    // A value above MAX_GOVERNANCE_EXECUTOR_VERSION names no defined lane: the runtime
    // setVersion fail-opens (chain keeps producing) while every node's next start throws in
    // validateOpModeGenesisOnly — an accepted write bricks restarts. Refused from this
    // release on; a pre-3.18 block that set it still replays, hence the version gate.
    SystemConfigPrecompiled systemConfigPrecompiled(hashImpl);
    auto setParameters = std::make_shared<PrecompiledExecResult>();
    CodecWrapper codec(hashImpl);
    auto const key =
        std::string(magic_enum::enum_name(bcos::ledger::SystemConfig::executor_version));
    auto const beyondLadder = std::to_string(bcos::ledger::MAX_GOVERNANCE_EXECUTOR_VERSION + 1);
    auto const callOn = [&](uint32_t blockVersion) {
        std::shared_ptr<BlockContext> blockContext =
            std::make_shared<BlockContext>(executive->blockContext().storage(), ledgerCache,
                executive->blockContext().hashHandler(), 1, h256(), utcTime(), blockVersion, false,
                backendStorage);
        auto executiveForVersion =
            std::make_shared<MockTransactionExecutive>(*blockContext, "", 100, 0);
        auto input = codec.encodeWithSig("setValueByKey(string,string)", key, beyondLadder);
        setParameters->m_input = bcos::ref(input);
        return systemConfigPrecompiled.call(executiveForVersion, setParameters);
    };

    // From this release on an undefined lane cannot be written by a transaction.
    BOOST_CHECK_THROW(
        callOn(static_cast<uint32_t>(protocol::BlockVersion::V3_18_0_VERSION)), PrecompiledError);
    // A pre-3.18 block that did set it still replays: the refusal is version-gated.
    BOOST_CHECK_NO_THROW(callOn(static_cast<uint32_t>(protocol::BlockVersion::V3_17_0_VERSION)));

    // The in-lane values remain writable (the ladder top itself is the genesis-only
    // OPSTACK case above; the Eth lane must keep its ordinary governance semantics).
    auto const setVersion = [&](uint32_t blockVersion, std::string const& value) {
        std::shared_ptr<BlockContext> blockContext =
            std::make_shared<BlockContext>(executive->blockContext().storage(), ledgerCache,
                executive->blockContext().hashHandler(), 1, h256(), utcTime(), blockVersion, false,
                backendStorage);
        auto executiveForVersion =
            std::make_shared<MockTransactionExecutive>(*blockContext, "", 100, 0);
        auto input = codec.encodeWithSig("setValueByKey(string,string)", key, value);
        setParameters->m_input = bcos::ref(input);
        return systemConfigPrecompiled.call(executiveForVersion, setParameters);
    };
    BOOST_CHECK_NO_THROW(setVersion(static_cast<uint32_t>(protocol::BlockVersion::V3_18_0_VERSION),
        std::to_string(bcos::ledger::ETHEREUM_EXECUTOR_VERSION)));
}

BOOST_AUTO_TEST_CASE(upgradeVersion)
{
    task::syncWait([this]() -> task::Task<void> {
        SystemConfigPrecompiled systemConfigPrecompiled(hashImpl);
        auto setParameters = std::make_shared<PrecompiledExecResult>();

        CodecWrapper codec(hashImpl);
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

        auto entry = co_await storage2::readOne(
            *backendStorage, executor_v1::StateKeyView(ledger::SYS_CONFIG, "bugfix_revert"));
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
            bcos::executor_v1::StateKeyView(ledger::SYS_CONFIG, "feature_sharding"));
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

BOOST_AUTO_TEST_CASE(web3ChainIdSharesParseWeb3ChainId)
{
    SystemConfigPrecompiled systemConfigPrecompiled(hashImpl);
    std::shared_ptr<LedgerCache> v39LedgerCache =
        std::make_shared<LedgerCache>(std::make_shared<bcos::test::MockLedger>());
    std::shared_ptr<BlockContext> v39Context =
        std::make_shared<BlockContext>(stateStorage, v39LedgerCache, hashImpl, 0, h256(), utcTime(),
            static_cast<uint32_t>(protocol::BlockVersion::V3_9_0_VERSION), false, backendStorage);
    auto v39Executive = std::make_shared<MockTransactionExecutive>(*v39Context, "", 100, 0);
    v39Executive->setStorageWrapper();

    CodecWrapper codec(hashImpl);
    auto setParameters = std::make_shared<PrecompiledExecResult>();
    auto trySet = [&](std::string const& value) {
        auto setInput = codec.encodeWithSig(
            "setValueByKey(string,string)", std::string(SYSTEM_KEY_WEB3_CHAIN_ID), value);
        setParameters->m_input = bcos::ref(setInput);
        return systemConfigPrecompiled.call(v39Executive, setParameters);
    };

    BOOST_CHECK_NO_THROW(trySet("1337"));
    BOOST_CHECK_NO_THROW(trySet("0x539"));
    BOOST_CHECK_THROW(trySet("-5"), PrecompiledError);
    BOOST_CHECK_THROW(trySet("-0"), PrecompiledError);
    BOOST_CHECK_THROW(trySet("not-a-number"), PrecompiledError);
    BOOST_CHECK_THROW(trySet("4294967296"), PrecompiledError);
}

// feature_l2_ethereum_compat is genesis-only. Features::validate refuses it, and this pins the
// refusal where an operator meets it: the governance setValueByKey transaction, which must fail
// rather than turn L2 mode on for a chain that was not born one. The message matters too --
// SystemConfigPrecompiled re-throws the errinfo_comment verbatim (SystemConfigPrecompiled.cpp:334)
// so what is asserted here is the revert reason the caller sees.
BOOST_AUTO_TEST_CASE(genesisOnlyFeatureIsRefusedByGovernance)
{
    SystemConfigPrecompiled systemConfigPrecompiled(hashImpl);
    auto setParameters = std::make_shared<PrecompiledExecResult>();
    CodecWrapper codec(hashImpl);

    auto setInput = codec.encodeWithSig("setValueByKey(string,string)",
        std::string("feature_l2_ethereum_compat"), std::string("1"));
    setParameters->m_input = bcos::ref(setInput);
    BOOST_CHECK_EXCEPTION(systemConfigPrecompiled.call(executive, setParameters), PrecompiledError,
        [](PrecompiledError const& e) {
            auto const* msg = boost::get_error_info<bcos::errinfo_comment>(e);
            return msg != nullptr && msg->find("genesis-only") != std::string::npos;
        });

    // The refusal is the feature rule, not the unknown-key rule: the key IS recognised, so a
    // neighbouring feature on the same channel is still settable by governance. (feature_op_jovian
    // was the control until OP forks moved to [op_fork_timestamps]; its bit 60 is now reserved.)
    setInput = codec.encodeWithSig("setValueByKey(string,string)",
        std::string("bugfix_eip161_1052_account_semantics"), std::string("1"));
    setParameters->m_input = bcos::ref(setInput);
    BOOST_CHECK_NO_THROW(systemConfigPrecompiled.call(executive, setParameters));
}

// OP mode (executor_version >= ledger::OPSTACK_EXECUTOR_VERSION) is chosen once at boot from
// the on-chain executor_version row: it decides the block producer (an external op-node over
// the Engine API), the scheduler slot and the fork schedule. This precompile is the only
// RUNTIME writer of that row -- genesis writes it directly, without validate() -- so refusing
// the value here is what makes the boundary un-crossable on a running chain. Versioned on
// 3.18.0 so replaying a pre-3.18.0 block that already wrote such a value still reproduces the
// old acceptance.
BOOST_AUTO_TEST_CASE(executorVersionOpModeIsNotGovernable)
{
    SystemConfigPrecompiled systemConfigPrecompiled(hashImpl);
    CodecWrapper codec(hashImpl);
    auto const executorVersionKey =
        std::string(magic_enum::enum_name(ledger::SystemConfig::executor_version));

    // Accepted values must actually be WRITTEN, not merely not-thrown: the guard sits before
    // the write, so a check that only asserts "no throw" would still pass if the whole setter
    // silently became a no-op.
    auto expectWritten = [&](PrecompiledExecResult::Ptr const& result) {
        bcos::s256 code = -1;
        codec.decode(bcos::ref(result->execResult()), code);
        BOOST_CHECK_EQUAL(code, (int)CODE_SUCCESS);
    };
    auto trySetAt = [&](protocol::BlockVersion blockVersion, std::string const& value) {
        auto ledgerCacheAt =
            std::make_shared<LedgerCache>(std::make_shared<bcos::test::MockLedger>());
        auto contextAt = std::make_shared<BlockContext>(stateStorage, ledgerCacheAt, hashImpl, 0,
            h256(), utcTime(), static_cast<uint32_t>(blockVersion), false, backendStorage);
        auto executiveAt = std::make_shared<MockTransactionExecutive>(*contextAt, "", 100, 0);
        executiveAt->setStorageWrapper();

        auto setParameters = std::make_shared<PrecompiledExecResult>();
        auto setInput =
            codec.encodeWithSig("setValueByKey(string,string)", executorVersionKey, value);
        setParameters->m_input = bcos::ref(setInput);
        return systemConfigPrecompiled.call(executiveAt, setParameters);
    };

    // 3.18.0: the OP boundary is closed. 3 is the first OP value; anything above it is OP too.
    BOOST_CHECK_THROW(trySetAt(protocol::BlockVersion::V3_18_0_VERSION, "3"), PrecompiledError);
    BOOST_CHECK_THROW(trySetAt(protocol::BlockVersion::V3_18_0_VERSION, "4"), PrecompiledError);
    // ...and only the OP boundary is closed: every value below it stays governable, so a v1
    // chain can still be moved onto the pure-Ethereum executor by governance.
    for (auto const* accepted : {"2", "1", "0"})
    {
        expectWritten(trySetAt(protocol::BlockVersion::V3_18_0_VERSION, accepted));
    }
    // Pre-3.18.0 blocks keep the old acceptance: the gate is the on-chain compatibility
    // version, and making the refusal unconditional would be an unversioned consensus change
    // that breaks replay/resync of a chain which already wrote 3.
    expectWritten(trySetAt(protocol::BlockVersion::V3_17_0_VERSION, "3"));
}

BOOST_AUTO_TEST_SUITE_END()
