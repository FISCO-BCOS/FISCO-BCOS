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

BOOST_AUTO_TEST_CASE(ShouldBuildMPT_RawAddressWithScenarioAThrowsPastActivation)
{
    ledger::Features features;
    features.set(ledger::Features::Flag::feature_raw_address);
    features.set(ledger::Features::Flag::feature_mpt_state_root);
    features.setActivationBlock(ledger::Features::Flag::feature_mpt_state_root, 100);

    // At and before the activation block scenario A stays on XOR, so no MPT is built and
    // the guard must not fire.
    BOOST_CHECK(!shouldBuildMPT(features, 99));
    BOOST_CHECK(!shouldBuildMPT(features, 100));
    // Past the boundary the MPT would be built: fail loudly instead of silently dropping
    // every binary-named account table.
    BOOST_CHECK_THROW(shouldBuildMPT(features, 101), InvalidMPTFlagMatrix);
    BOOST_CHECK_THROW(shouldBuildMPT(features, 1000), InvalidMPTFlagMatrix);
}

BOOST_AUTO_TEST_CASE(ShouldBuildMPT_RawAddressWithScenarioBThrowsEveryBlock)
{
    ledger::Features features;
    features.set(ledger::Features::Flag::feature_raw_address);
    features.set(ledger::Features::Flag::feature_l2_ethereum_compat);

    // Scenario B builds the MPT from genesis on, so the guard fires at every block.
    BOOST_CHECK_THROW(shouldBuildMPT(features, 0), InvalidMPTFlagMatrix);
    BOOST_CHECK_THROW(shouldBuildMPT(features, 1000), InvalidMPTFlagMatrix);
}

BOOST_AUTO_TEST_CASE(ShouldBuildMPT_RawAddressAloneUnchanged)
{
    ledger::Features features;
    features.set(ledger::Features::Flag::feature_raw_address);

    // Without an MPT flag the function keeps its legacy answer: XOR path, no throw.
    BOOST_CHECK_NO_THROW(BOOST_CHECK(!shouldBuildMPT(features, 0)));
    BOOST_CHECK_NO_THROW(BOOST_CHECK(!shouldBuildMPT(features, 1000)));
}

BOOST_AUTO_TEST_SUITE_END()
