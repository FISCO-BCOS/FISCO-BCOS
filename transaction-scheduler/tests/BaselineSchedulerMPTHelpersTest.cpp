#include "bcos-transaction-scheduler/BaselineSchedulerMPTHelpers.h"
#include "bcos-framework/ledger/Features.h"
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::scheduler_v1;

BOOST_AUTO_TEST_SUITE(BaselineSchedulerMPTHelpersSuite)

BOOST_AUTO_TEST_CASE(BothFlagsOff_AlwaysXOR)
{
    ledger::Features features;
    BOOST_CHECK(!shouldBuildMPT(features, 0));
    BOOST_CHECK(!shouldBuildMPT(features, 1000));
}

BOOST_AUTO_TEST_CASE(ScenarioA_StrictlyGreaterThanActivation)
{
    ledger::Features features;
    features.set(ledger::Features::Flag::feature_mpt_state_root);
    features.setActivationBlock(ledger::Features::Flag::feature_mpt_state_root, 100);

    BOOST_CHECK(!shouldBuildMPT(features, 99));
    // The activation block N itself stays on XOR: strict-greater is the transition boundary
    BOOST_CHECK(!shouldBuildMPT(features, 100));
    BOOST_CHECK(shouldBuildMPT(features, 101));
    BOOST_CHECK(shouldBuildMPT(features, 1000));
}

BOOST_AUTO_TEST_CASE(ScenarioB_FromGenesis)
{
    ledger::Features features;
    features.set(ledger::Features::Flag::feature_l2_ethereum_compat);
    // No activation block recorded: scenario B does not need one

    BOOST_CHECK(shouldBuildMPT(features, 0));
    BOOST_CHECK(shouldBuildMPT(features, 1));
    BOOST_CHECK(shouldBuildMPT(features, 1000));
}

BOOST_AUTO_TEST_CASE(ScenarioBPriorityOverScenarioA)
{
    ledger::Features features;
    features.set(ledger::Features::Flag::feature_l2_ethereum_compat);
    features.set(ledger::Features::Flag::feature_mpt_state_root);
    features.setActivationBlock(ledger::Features::Flag::feature_mpt_state_root, 100);

    // Scenario B wins regardless of the scenario-A activation boundary; a branch
    // reordering that consulted feature_mpt_state_root first would fail at 99/100
    BOOST_CHECK(shouldBuildMPT(features, 99));
    BOOST_CHECK(shouldBuildMPT(features, 100));
    BOOST_CHECK(shouldBuildMPT(features, 101));
}

BOOST_AUTO_TEST_CASE(ScenarioA_FlagSetButActivationUnknown_DoesNotBuild)
{
    ledger::Features features;
    features.set(ledger::Features::Flag::feature_mpt_state_root);
    // No setActivationBlock: activationBlockOf reports -1 (bare set(), no storage load).
    // Without the >= 0 guard, blockNumber > -1 would silently enable MPT everywhere.

    BOOST_CHECK(!shouldBuildMPT(features, 0));
    BOOST_CHECK(!shouldBuildMPT(features, 1000));
}

BOOST_AUTO_TEST_CASE(FlagMatrix_NoL2FlagAlwaysPasses)
{
    ledger::Features features;
    BOOST_CHECK_NO_THROW(validateMPTFlagMatrix(features));

    // Scenario A alone is a legal matrix regardless of its activation block: the
    // guard only polices scenario B's genesis-only assumption.
    features.set(ledger::Features::Flag::feature_mpt_state_root);
    features.setActivationBlock(ledger::Features::Flag::feature_mpt_state_root, 500);
    BOOST_CHECK_NO_THROW(validateMPTFlagMatrix(features));
}

BOOST_AUTO_TEST_CASE(FlagMatrix_L2AtGenesisPasses)
{
    ledger::Features features;
    features.set(ledger::Features::Flag::feature_l2_ethereum_compat);
    features.setActivationBlock(ledger::Features::Flag::feature_l2_ethereum_compat, 0);
    BOOST_CHECK_NO_THROW(validateMPTFlagMatrix(features));
}

BOOST_AUTO_TEST_CASE(FlagMatrix_L2MidChainThrows)
{
    ledger::Features features;
    features.set(ledger::Features::Flag::feature_l2_ethereum_compat);
    features.setActivationBlock(ledger::Features::Flag::feature_l2_ethereum_compat, 42);
    BOOST_CHECK_THROW(validateMPTFlagMatrix(features), InvalidMPTFlagMatrix);
}

BOOST_AUTO_TEST_CASE(FlagMatrix_L2ActivationUnknownThrows)
{
    ledger::Features features;
    features.set(ledger::Features::Flag::feature_l2_ethereum_compat);
    // Bare set() without a storage load: activationBlockOf is -1. An unverifiable
    // activation must not pass a consistency check.
    BOOST_CHECK_THROW(validateMPTFlagMatrix(features), InvalidMPTFlagMatrix);
}

BOOST_AUTO_TEST_SUITE_END()
