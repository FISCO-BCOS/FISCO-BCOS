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

BOOST_AUTO_TEST_CASE(executorVersionOffTheOpstackSlotIsRefused)
{
    // The other half of the genesis-only invariant: the write-path gate refuses a write TO the
    // OPSTACK slot, and this refuses a write OFF it. The runtime freeze keeps the running process
    // on the OP lane but still lands the row, and the next start re-derives the lane from that
    // row — so an accepted downgrade moves the chain off OP at the first restart. Version-scoped
    // like the other gates, so a pre-3.18 block that moved it still replays.
    SystemConfigPrecompiled systemConfigPrecompiled(hashImpl);
    auto setParameters = std::make_shared<PrecompiledExecResult>();
    CodecWrapper codec(hashImpl);
    auto const key =
        std::string(magic_enum::enum_name(bcos::ledger::SystemConfig::executor_version));
    auto const opstack = std::to_string(bcos::ledger::OPSTACK_EXECUTOR_VERSION);
    auto const callOn = [&](uint32_t blockVersion, std::string const& value) {
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

    // "No throw" cannot tell an accepted write from an ignored one: read the row back the way a
    // client would.
    auto const readBack = [&](uint32_t blockVersion) {
        auto readParameters = std::make_shared<PrecompiledExecResult>();
        auto readInput = codec.encodeWithSig("getValueByKey(string)", key);
        readParameters->m_input = bcos::ref(readInput);
        std::shared_ptr<BlockContext> blockContext =
            std::make_shared<BlockContext>(executive->blockContext().storage(), ledgerCache,
                executive->blockContext().hashHandler(), 1, h256(), utcTime(), blockVersion, false,
                backendStorage);
        auto readExecutive = std::make_shared<MockTransactionExecutive>(*blockContext, "", 100, 0);
        auto const result = systemConfigPrecompiled.call(readExecutive, readParameters);
        std::string value;
        codec.decode(bcos::ref(result->execResult()), value);
        return value;
    };
    auto const v3_17 = static_cast<uint32_t>(protocol::BlockVersion::V3_17_0_VERSION);
    auto const v3_18 = static_cast<uint32_t>(protocol::BlockVersion::V3_18_0_VERSION);

    // An OP chain carries the OPSTACK row; a pre-3.18 block may still land it through here.
    BOOST_CHECK_NO_THROW(callOn(v3_17, opstack));
    BOOST_CHECK_EQUAL(readBack(v3_17), opstack);

    BOOST_CHECK_THROW(
        callOn(v3_18, std::to_string(bcos::ledger::ETHEREUM_EXECUTOR_VERSION)), PrecompiledError);
    BOOST_CHECK_EQUAL(readBack(v3_18), opstack);  // refused: the OP row is still there
    // The refused write left the row alone: the next write is still judged against the OP slot.
    BOOST_CHECK_THROW(callOn(v3_18, std::to_string(0)), PrecompiledError);
    BOOST_CHECK_EQUAL(readBack(v3_18), opstack);
    // A pre-3.18 block that moved the row off OP still replays.
    BOOST_CHECK_NO_THROW(callOn(v3_17, std::to_string(bcos::ledger::ETHEREUM_EXECUTOR_VERSION)));
    BOOST_CHECK_EQUAL(readBack(v3_17), std::to_string(bcos::ledger::ETHEREUM_EXECUTOR_VERSION));
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

// feature_l2_ethereum_compat is gone, not merely gated: bit 57 is now the tombstone
// reserved_removed_l2_ethereum_compat (Features.h), and the old name is deliberately NOT
// recognised -- Features::string2Flag throws NoSuchFeatureError for it, so a config or
// transaction still carrying the name fails loudly instead of silently enabling nothing.
// The lane it used to switch on is a genesis property now (executor_version >= 2). This
// pins the refusal where an operator meets it: the governance setValueByKey transaction
// rejects the key as unknown ("unsupported key", SystemConfigPrecompiled::validate, which
// re-throws with the errinfo_comment the caller sees as the revert reason).
BOOST_AUTO_TEST_CASE(removedL2FlagNameIsRefusedByGovernance)
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
            return msg != nullptr && msg->find("unsupported key") != std::string::npos;
        });

    // The refusal is the unknown-key rule, so a recognised feature on the same channel is
    // still settable by governance.
    setInput = codec.encodeWithSig("setValueByKey(string,string)",
        std::string("bugfix_eip161_1052_account_semantics"), std::string("1"));
    setParameters->m_input = bcos::ref(setInput);
    BOOST_CHECK_NO_THROW(systemConfigPrecompiled.call(executive, setParameters));
}

// The Ethereum lane (executor_version >= ledger::ETHEREUM_EXECUTOR_VERSION: L1 EL at 2, OP-Stack
// at 3+) is chosen once at boot from the on-chain executor_version row: it decides the
// account-table lane, the state-root scheme, the block producer and the fork schedule. This
// precompile is the only RUNTIME writer of that row -- genesis writes it directly, without
// validate() -- so refusing the crossing here is what makes the boundary un-crossable on a
// running chain. Versioned on 3.18.0 so replaying a pre-3.18.0 block that already wrote such a
// value still reproduces the old acceptance.
BOOST_AUTO_TEST_CASE(executorVersionEthereumLaneIsNotGovernable)
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

    // 3.18.0: the Ethereum boundary is closed. 2 is the first Ethereum-lane value (L1 EL);
    // 3 and above are OP -- every value at or over the boundary is refused.
    for (auto const* refused : {"2", "3", "4"})
    {
        BOOST_CHECK_EXCEPTION(trySetAt(protocol::BlockVersion::V3_18_0_VERSION, refused),
            PrecompiledError, [](PrecompiledError const& e) {
                auto const* msg = boost::get_error_info<bcos::errinfo_comment>(e);
                return msg != nullptr && msg->find("is a genesis property") != std::string::npos;
            });
    }
    // ...and only the crossing is closed: consortium-lane values stay governable.
    for (auto const* accepted : {"1", "0"})
    {
        expectWritten(trySetAt(protocol::BlockVersion::V3_18_0_VERSION, accepted));
    }
    // Pre-3.18.0 blocks keep the old acceptance: the gate is the on-chain compatibility
    // version, and making the refusal unconditional would be an unversioned consensus change
    // that breaks replay/resync of a chain which already wrote 3.
    expectWritten(trySetAt(protocol::BlockVersion::V3_17_0_VERSION, "3"));
}

BOOST_AUTO_TEST_SUITE_END()
