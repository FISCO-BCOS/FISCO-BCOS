#include "bcos-transaction-scheduler/BaselineSchedulerMPTHelpers.h"
#include "bcos-framework/ledger/Features.h"
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::scheduler_v1;

BOOST_AUTO_TEST_SUITE(BaselineSchedulerMPTHelpersSuite)

// The legacy FISCO lane (executor_version 0/1) with no feature_mpt_state_root: XOR forever.
BOOST_AUTO_TEST_CASE(LegacyLane_AlwaysXOR)
{
    ledger::Features features;
    BOOST_CHECK(!shouldBuildMPT(0, features, 0));
    BOOST_CHECK(!shouldBuildMPT(0, features, 1000));
    // executor_version 1 is still the legacy lane.
    BOOST_CHECK(!shouldBuildMPT(1, features, 1000));
}

BOOST_AUTO_TEST_CASE(ScenarioA_StrictlyGreaterThanActivation)
{
    ledger::Features features;
    features.set(ledger::Features::Flag::feature_mpt_state_root);
    features.setActivationBlock(ledger::Features::Flag::feature_mpt_state_root, 100);

    BOOST_CHECK(!shouldBuildMPT(0, features, 99));
    // The activation block N itself stays on XOR: strict-greater is the transition boundary
    BOOST_CHECK(!shouldBuildMPT(0, features, 100));
    BOOST_CHECK(shouldBuildMPT(0, features, 101));
    BOOST_CHECK(shouldBuildMPT(0, features, 1000));
}

BOOST_AUTO_TEST_CASE(ScenarioB_FromGenesis)
{
    // Scenario B is the Ethereum lane (executor_version >= 2): the MPT is built from
    // genesis on, no feature flag involved.
    ledger::Features features;

    BOOST_CHECK(shouldBuildMPT(ledger::ETHEREUM_EXECUTOR_VERSION, features, 0));
    BOOST_CHECK(shouldBuildMPT(ledger::ETHEREUM_EXECUTOR_VERSION, features, 1));
    BOOST_CHECK(shouldBuildMPT(ledger::ETHEREUM_EXECUTOR_VERSION, features, 1000));
    // The OP lane (>= 3) is an Ethereum lane too.
    BOOST_CHECK(shouldBuildMPT(ledger::OPSTACK_EXECUTOR_VERSION, features, 0));
}

BOOST_AUTO_TEST_CASE(ScenarioBPriorityOverScenarioA)
{
    ledger::Features features;
    features.set(ledger::Features::Flag::feature_mpt_state_root);
    features.setActivationBlock(ledger::Features::Flag::feature_mpt_state_root, 100);

    // The Ethereum lane wins regardless of the scenario-A activation boundary; a branch
    // reordering that consulted feature_mpt_state_root first would fail at 99/100
    BOOST_CHECK(shouldBuildMPT(ledger::ETHEREUM_EXECUTOR_VERSION, features, 99));
    BOOST_CHECK(shouldBuildMPT(ledger::ETHEREUM_EXECUTOR_VERSION, features, 100));
    BOOST_CHECK(shouldBuildMPT(ledger::ETHEREUM_EXECUTOR_VERSION, features, 101));
}

BOOST_AUTO_TEST_CASE(ScenarioA_FlagSetButActivationUnknown_DoesNotBuild)
{
    ledger::Features features;
    features.set(ledger::Features::Flag::feature_mpt_state_root);
    // No setActivationBlock: activationBlockOf reports -1 (bare set(), no storage load).
    // Without the >= 0 guard, blockNumber > -1 would silently enable MPT everywhere.

    BOOST_CHECK(!shouldBuildMPT(0, features, 0));
    BOOST_CHECK(!shouldBuildMPT(0, features, 1000));
}

// OP mode (executor_version >= OPSTACK_EXECUTOR_VERSION) is genesis-only: its activation
// block must be 0. Below the OP lane this guard polices nothing — the lane boundary
// itself is genesis-fixed and the SystemConfigPrecompiled refuses governance writes
// crossing it in both directions.
BOOST_AUTO_TEST_CASE(OpMode_GenesisBoundActivationPasses)
{
    BOOST_CHECK_NO_THROW(validateOpModeGenesisOnly(ledger::OPSTACK_EXECUTOR_VERSION, 0));

    // Legacy and Eth lanes: no genesis-only constraint in this guard.
    BOOST_CHECK_NO_THROW(validateOpModeGenesisOnly(0, 0));
    BOOST_CHECK_NO_THROW(validateOpModeGenesisOnly(ledger::ETHEREUM_EXECUTOR_VERSION, 0));
    BOOST_CHECK_NO_THROW(validateOpModeGenesisOnly(ledger::ETHEREUM_EXECUTOR_VERSION, 7));
}

BOOST_AUTO_TEST_CASE(OpMode_LateActivationThrows)
{
    BOOST_CHECK_THROW(validateOpModeGenesisOnly(ledger::OPSTACK_EXECUTOR_VERSION, 42),
        InvalidExecutorVersionGenesis);
}

BOOST_AUTO_TEST_CASE(OpMode_AboveTheLadderSaturatesAndLateActivationIsRefused)
{
    // Above the newest declared lane there is no lane of its own: the scheduler saturates such a
    // value onto the newest WIRED slot (MultiVersionScheduler::setVersion, pinned by the
    // libinitializer suite's setVersionSaturatesToNewestWiredSlot), so boot accepts it. Refusing
    // it here would strand a chain that wrote the row before 3.18 — nothing bounded that key then
    // — with no way to lower it (the precompile refuses writes at or above OPSTACK).
    BOOST_CHECK_NO_THROW(validateOpModeGenesisOnly(ledger::OPSTACK_EXECUTOR_VERSION + 1, 0));
    // The OP lane's own genesis-only precondition still applies to every value at or above the
    // slot.
    BOOST_CHECK_THROW(validateOpModeGenesisOnly(ledger::OPSTACK_EXECUTOR_VERSION, 1),
        InvalidExecutorVersionGenesis);
    BOOST_CHECK_THROW(validateOpModeGenesisOnly(ledger::OPSTACK_EXECUTOR_VERSION + 1, 1),
        InvalidExecutorVersionGenesis);
}

BOOST_AUTO_TEST_SUITE_END()
