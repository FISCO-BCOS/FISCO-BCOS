#include "bcos-framework/ledger/Features.h"
#include "bcos-transaction-scheduler/BaselineSchedulerMPTHelpers.h"
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::scheduler_v1;

// feature_raw_address and the MPT state root COEXIST: the MPT delta scan classifies both
// account-table layouts (Classify.h parseAccountTable accepts the 40-hex and the 20-byte
// raw-address forms), and the first-touch flat back-fill reads through the chain's
// AddressTableMode (FlatToMPT.h readFlatAccountMeta). These tests pin the acceptance — the
// former mutual-exclusion guard (rejectRawAddressWithMPT and the raw_address arm of
// validateMPTFlagMatrix) is deleted, not relaxed.
BOOST_AUTO_TEST_SUITE(RawAddressMPTGuardSuite)

BOOST_AUTO_TEST_CASE(FlagMatrix_RawAddressWithMPTStateRootAccepted)
{
    ledger::Features features;
    features.set(ledger::Features::Flag::feature_raw_address);
    features.set(ledger::Features::Flag::feature_mpt_state_root);
    features.setActivationBlock(ledger::Features::Flag::feature_mpt_state_root, 100);
    BOOST_CHECK_NO_THROW(validateMPTFlagMatrix(features));
}

BOOST_AUTO_TEST_CASE(FlagMatrix_RawAddressWithL2AtGenesisAccepted)
{
    ledger::Features features;
    features.set(ledger::Features::Flag::feature_raw_address);
    features.set(ledger::Features::Flag::feature_l2_ethereum_compat);
    features.setActivationBlock(ledger::Features::Flag::feature_l2_ethereum_compat, 0);
    BOOST_CHECK_NO_THROW(validateMPTFlagMatrix(features));
}

BOOST_AUTO_TEST_CASE(FlagMatrix_RawAddressAlonePasses)
{
    ledger::Features features;
    features.set(ledger::Features::Flag::feature_raw_address);
    BOOST_CHECK_NO_THROW(validateMPTFlagMatrix(features));
}

BOOST_AUTO_TEST_CASE(FlagMatrix_MPTFlagsWithoutRawAddressStillPass)
{
    // Regression: the raw_address acceptance must not disturb the previously-legal matrices.
    ledger::Features scenarioA;
    scenarioA.set(ledger::Features::Flag::feature_mpt_state_root);
    scenarioA.setActivationBlock(ledger::Features::Flag::feature_mpt_state_root, 500);
    BOOST_CHECK_NO_THROW(validateMPTFlagMatrix(scenarioA));

    ledger::Features scenarioB;
    scenarioB.set(ledger::Features::Flag::feature_l2_ethereum_compat);
    scenarioB.setActivationBlock(ledger::Features::Flag::feature_l2_ethereum_compat, 0);
    BOOST_CHECK_NO_THROW(validateMPTFlagMatrix(scenarioB));
}

// The surviving half of validateMPTFlagMatrix is scenario B's genesis-only rule; it applies
// with or without raw_address.
BOOST_AUTO_TEST_CASE(FlagMatrix_L2MidChainStillThrowsEvenWithRawAddress)
{
    ledger::Features features;
    features.set(ledger::Features::Flag::feature_raw_address);
    features.set(ledger::Features::Flag::feature_l2_ethereum_compat);
    features.setActivationBlock(ledger::Features::Flag::feature_l2_ethereum_compat, 42);
    BOOST_CHECK_THROW(validateMPTFlagMatrix(features), InvalidMPTFlagMatrix);
}

// shouldBuildMPT stays a PURE state-root predicate: raw_address changes the account-table
// encoding, never whether a block builds an MPT.
BOOST_AUTO_TEST_CASE(ShouldBuildMPT_UnchangedByRawAddress_ScenarioA)
{
    ledger::Features features;
    features.set(ledger::Features::Flag::feature_raw_address);
    features.set(ledger::Features::Flag::feature_mpt_state_root);
    features.setActivationBlock(ledger::Features::Flag::feature_mpt_state_root, 100);

    // The activation block N itself stays on XOR (strictly-greater boundary), then MPT.
    BOOST_CHECK(!shouldBuildMPT(features, 99));
    BOOST_CHECK(!shouldBuildMPT(features, 100));
    BOOST_CHECK(shouldBuildMPT(features, 101));
    BOOST_CHECK(shouldBuildMPT(features, 1000));
}

BOOST_AUTO_TEST_CASE(ShouldBuildMPT_UnchangedByRawAddress_ScenarioB)
{
    ledger::Features features;
    features.set(ledger::Features::Flag::feature_raw_address);
    features.set(ledger::Features::Flag::feature_l2_ethereum_compat);

    // Scenario B builds from genesis on, raw_address or not.
    BOOST_CHECK(shouldBuildMPT(features, 0));
    BOOST_CHECK(shouldBuildMPT(features, 1000));

    // raw_address WITHOUT an MPT flag still builds nothing.
    ledger::Features rawOnly;
    rawOnly.set(ledger::Features::Flag::feature_raw_address);
    BOOST_CHECK(!shouldBuildMPT(rawOnly, 0));
    BOOST_CHECK(!shouldBuildMPT(rawOnly, 1000));
}

BOOST_AUTO_TEST_SUITE_END()
