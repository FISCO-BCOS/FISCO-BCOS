#include "bcos-framework/ledger/Features.h"
#include "bcos-transaction-scheduler/BaselineSchedulerMPTHelpers.h"
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::scheduler_v1;

// feature_raw_address and the MPT state root coexist ONLY on the mode-aware mainline (v1
// baseline) lane: the MPT delta scan classifies both account-table layouts (Classify.h
// parseAccountTable accepts the 40-hex and the 20-byte raw-address forms), and the
// first-touch flat back-fill reads through the chain's AddressTableMode (FlatToMPT.h
// readFlatAccountMeta). The hex-only lanes (OP / Eth engine) keep a loud-fail guard:
// validateMPTFlagMatrix rejects raw_address + feature_l2_ethereum_compat at boot, and
// rejectRawAddressOnEngineLanes re-checks raw_address per block (mid-chain governance
// activation is invisible to the boot guard).
BOOST_AUTO_TEST_SUITE(RawAddressMPTGuardSuite)

BOOST_AUTO_TEST_CASE(FlagMatrix_RawAddressWithMPTStateRootAccepted)
{
    ledger::Features features;
    features.set(ledger::Features::Flag::feature_raw_address);
    features.set(ledger::Features::Flag::feature_mpt_state_root);
    features.setActivationBlock(ledger::Features::Flag::feature_mpt_state_root, 100);
    BOOST_CHECK_NO_THROW(validateMPTFlagMatrix(features));
}

// raw_address + L2 is rejected again: the OP lane's Storage2State bridge and the
// ethereum-executor are hex-only, so the combination would split mode-aware RPC reads from
// hex-only executor writes.
BOOST_AUTO_TEST_CASE(FlagMatrix_RawAddressWithL2AtGenesisRejected)
{
    ledger::Features features;
    features.set(ledger::Features::Flag::feature_raw_address);
    features.set(ledger::Features::Flag::feature_l2_ethereum_compat);
    features.setActivationBlock(ledger::Features::Flag::feature_l2_ethereum_compat, 0);
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
    // Regression: the raw_address rejection must not disturb the previously-legal matrices.
    ledger::Features scenarioA;
    scenarioA.set(ledger::Features::Flag::feature_mpt_state_root);
    scenarioA.setActivationBlock(ledger::Features::Flag::feature_mpt_state_root, 500);
    BOOST_CHECK_NO_THROW(validateMPTFlagMatrix(scenarioA));

    ledger::Features scenarioB;
    scenarioB.set(ledger::Features::Flag::feature_l2_ethereum_compat);
    scenarioB.setActivationBlock(ledger::Features::Flag::feature_l2_ethereum_compat, 0);
    BOOST_CHECK_NO_THROW(validateMPTFlagMatrix(scenarioB));
}

// The other surviving half of validateMPTFlagMatrix is scenario B's genesis-only rule; it
// applies with or without raw_address.
BOOST_AUTO_TEST_CASE(FlagMatrix_L2MidChainStillThrowsEvenWithRawAddress)
{
    ledger::Features features;
    features.set(ledger::Features::Flag::feature_raw_address);
    features.set(ledger::Features::Flag::feature_l2_ethereum_compat);
    features.setActivationBlock(ledger::Features::Flag::feature_l2_ethereum_compat, 42);
    BOOST_CHECK_THROW(validateMPTFlagMatrix(features), InvalidMPTFlagMatrix);
}

// The per-block guard on the hex-only lanes: raw_address throws whatever the MPT flags say
// (the OP/Eth executors are hex-only with or without an MPT root); without raw_address every
// matrix passes.
BOOST_AUTO_TEST_CASE(EngineLaneGuard_RawAddressThrowsAtAnyBlock)
{
    ledger::Features features;
    features.set(ledger::Features::Flag::feature_raw_address);
    BOOST_CHECK_THROW(rejectRawAddressOnEngineLanes(features, 0), InvalidMPTFlagMatrix);
    BOOST_CHECK_THROW(rejectRawAddressOnEngineLanes(features, 1000), InvalidMPTFlagMatrix);

    ledger::Features scenarioA;
    scenarioA.set(ledger::Features::Flag::feature_raw_address);
    scenarioA.set(ledger::Features::Flag::feature_mpt_state_root);
    scenarioA.setActivationBlock(ledger::Features::Flag::feature_mpt_state_root, 100);
    BOOST_CHECK_THROW(rejectRawAddressOnEngineLanes(scenarioA, 101), InvalidMPTFlagMatrix);

    ledger::Features noRaw;
    noRaw.set(ledger::Features::Flag::feature_l2_ethereum_compat);
    noRaw.setActivationBlock(ledger::Features::Flag::feature_l2_ethereum_compat, 0);
    BOOST_CHECK_NO_THROW(rejectRawAddressOnEngineLanes(noRaw, 1000));
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
