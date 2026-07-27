#include "bcos-framework/ledger/Features.h"
#include "bcos-transaction-scheduler/BaselineSchedulerMPTHelpers.h"
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::scheduler_v1;

BOOST_AUTO_TEST_SUITE(RawAddressMPTGuardSuite)

BOOST_AUTO_TEST_CASE(FlagMatrix_RawAddressWithMPTStateRootThrows)
{
    ledger::Features features;
    features.set(ledger::Features::Flag::feature_raw_address);
    features.set(ledger::Features::Flag::feature_mpt_state_root);
    features.setActivationBlock(ledger::Features::Flag::feature_mpt_state_root, 100);
    BOOST_CHECK_THROW(validateMPTFlagMatrix(features), InvalidMPTFlagMatrix);
}

BOOST_AUTO_TEST_CASE(FlagMatrix_RawAddressWithL2Throws)
{
    ledger::Features features;
    features.set(ledger::Features::Flag::feature_raw_address);
    features.set(ledger::Features::Flag::feature_l2_ethereum_compat);
    features.setActivationBlock(ledger::Features::Flag::feature_l2_ethereum_compat, 0);
    // Must throw for the raw_address pairing even though the L2 flag alone (genesis
    // activation) is a legal matrix.
    BOOST_CHECK_THROW(validateMPTFlagMatrix(features), InvalidMPTFlagMatrix);
}

BOOST_AUTO_TEST_CASE(FlagMatrix_RawAddressAlonePasses)
{
    ledger::Features features;
    features.set(ledger::Features::Flag::feature_raw_address);
    BOOST_CHECK_NO_THROW(validateMPTFlagMatrix(features));
}

BOOST_AUTO_TEST_CASE(FlagMatrix_MPTFlagsWithoutRawAddressStillPass)
{
    // Regression: the raw_address guard must not reject the previously-legal matrices.
    ledger::Features scenarioA;
    scenarioA.set(ledger::Features::Flag::feature_mpt_state_root);
    scenarioA.setActivationBlock(ledger::Features::Flag::feature_mpt_state_root, 500);
    BOOST_CHECK_NO_THROW(validateMPTFlagMatrix(scenarioA));

    ledger::Features scenarioB;
    scenarioB.set(ledger::Features::Flag::feature_l2_ethereum_compat);
    scenarioB.setActivationBlock(ledger::Features::Flag::feature_l2_ethereum_compat, 0);
    BOOST_CHECK_NO_THROW(validateMPTFlagMatrix(scenarioB));
}

// rejectRawAddressWithMPT is called from INSIDE the shouldBuildMPT branch, so its own job is
// just "is raw_address in the way?" — the block-number question is already answered by the
// call site. These cases pin the two states, and the gate-composition cases below pin that the
// pair together reproduces the behavior the old throwing shouldBuildMPT had.
BOOST_AUTO_TEST_CASE(PerBlockGuard_RawAddressThrows)
{
    ledger::Features features;
    features.set(ledger::Features::Flag::feature_raw_address);
    features.set(ledger::Features::Flag::feature_mpt_state_root);
    features.setActivationBlock(ledger::Features::Flag::feature_mpt_state_root, 100);

    BOOST_CHECK_THROW(rejectRawAddressWithMPT(features, 101), InvalidMPTFlagMatrix);
}

BOOST_AUTO_TEST_CASE(PerBlockGuard_WithoutRawAddressPasses)
{
    ledger::Features features;
    features.set(ledger::Features::Flag::feature_mpt_state_root);
    features.setActivationBlock(ledger::Features::Flag::feature_mpt_state_root, 100);

    BOOST_CHECK_NO_THROW(rejectRawAddressWithMPT(features, 101));
    BOOST_CHECK(shouldBuildMPT(features, 101));
}

// Gate composition, scenario A: the guard only runs where the predicate said yes, so at and
// before the activation block nothing fires (XOR path), and past the boundary it does.
BOOST_AUTO_TEST_CASE(GateComposition_ScenarioAFiresOnlyPastActivation)
{
    ledger::Features features;
    features.set(ledger::Features::Flag::feature_raw_address);
    features.set(ledger::Features::Flag::feature_mpt_state_root);
    features.setActivationBlock(ledger::Features::Flag::feature_mpt_state_root, 100);

    // Mirrors coExecuteBlock: predicate first, guard only inside the branch.
    auto executeGate = [&](protocol::BlockNumber blockNumber) {
        if (shouldBuildMPT(features, blockNumber))
        {
            rejectRawAddressWithMPT(features, blockNumber);
        }
    };

    BOOST_CHECK_NO_THROW(executeGate(99));
    BOOST_CHECK_NO_THROW(executeGate(100));
    BOOST_CHECK_THROW(executeGate(101), InvalidMPTFlagMatrix);
    BOOST_CHECK_THROW(executeGate(1000), InvalidMPTFlagMatrix);

    // The predicate itself stays PURE — it answers the state-root question and nothing else,
    // even for the block numbers the gate rejects.
    BOOST_CHECK(!shouldBuildMPT(features, 100));
    BOOST_CHECK(shouldBuildMPT(features, 101));
    BOOST_CHECK(shouldBuildMPT(features, 1000));
}

// Gate composition, scenario B: the MPT is built from genesis on, so the gate fires at every
// block; and raw_address WITHOUT an MPT flag never reaches the guard at all.
BOOST_AUTO_TEST_CASE(GateComposition_ScenarioBFiresEveryBlockRawAddressAloneNever)
{
    auto executeGate = [](ledger::Features const& features, protocol::BlockNumber blockNumber) {
        if (shouldBuildMPT(features, blockNumber))
        {
            rejectRawAddressWithMPT(features, blockNumber);
        }
    };

    ledger::Features scenarioB;
    scenarioB.set(ledger::Features::Flag::feature_raw_address);
    scenarioB.set(ledger::Features::Flag::feature_l2_ethereum_compat);
    BOOST_CHECK_THROW(executeGate(scenarioB, 0), InvalidMPTFlagMatrix);
    BOOST_CHECK_THROW(executeGate(scenarioB, 1000), InvalidMPTFlagMatrix);
    BOOST_CHECK(shouldBuildMPT(scenarioB, 0));
    BOOST_CHECK(shouldBuildMPT(scenarioB, 1000));

    ledger::Features rawOnly;
    rawOnly.set(ledger::Features::Flag::feature_raw_address);
    BOOST_CHECK_NO_THROW(executeGate(rawOnly, 0));
    BOOST_CHECK_NO_THROW(executeGate(rawOnly, 1000));
    BOOST_CHECK(!shouldBuildMPT(rawOnly, 0));
    BOOST_CHECK(!shouldBuildMPT(rawOnly, 1000));
}

BOOST_AUTO_TEST_SUITE_END()
