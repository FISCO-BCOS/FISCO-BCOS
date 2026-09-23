#include "bcos-framework/ledger/Features.h"
#include "bcos-transaction-scheduler/BaselineSchedulerMPTHelpers.h"
#include <bcos-tool/Exceptions.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::scheduler_v1;

// The MPT state-root predicate and its boot guard no longer involve the account-table
// encoding: the encoding is a node-local physical layout (nodeAddressTableMode), the MPT
// delta scan classifies both layouts (Classify.h parseAccountTable), and the deprecated
// feature_raw_address flag drives nothing — setting it is accepted with a warning, and
// the hex-only lanes' naming constraint is enforced by libinitializer's boot-time
// resolveNodeAddressTableMode instead of a flag matrix.
BOOST_AUTO_TEST_SUITE(RawAddressMPTGuardSuite)

BOOST_AUTO_TEST_CASE(FlagMatrix_MPTStateRootAccepted)
{
    ledger::Features features;
    features.set(ledger::Features::Flag::feature_mpt_state_root);
    features.setActivationBlock(ledger::Features::Flag::feature_mpt_state_root, 100);
    BOOST_CHECK_NO_THROW(validateMPTFlagMatrix(features));
}

// Even with the deprecated raw_address flag set in-memory (it cannot reach this state
// through governance any more), the matrix only judges the L2 flag.
BOOST_AUTO_TEST_CASE(FlagMatrix_DeprecatedRawAddressFlagIsInert)
{
    ledger::Features features;
    features.set(ledger::Features::Flag::feature_raw_address);
    BOOST_CHECK_NO_THROW(validateMPTFlagMatrix(features));

    features.set(ledger::Features::Flag::feature_l2_ethereum_compat);
    features.setActivationBlock(ledger::Features::Flag::feature_l2_ethereum_compat, 0);
    BOOST_CHECK_NO_THROW(validateMPTFlagMatrix(features));
}

BOOST_AUTO_TEST_CASE(FlagMatrix_L2MidChainStillThrows)
{
    ledger::Features features;
    features.set(ledger::Features::Flag::feature_l2_ethereum_compat);
    features.setActivationBlock(ledger::Features::Flag::feature_l2_ethereum_compat, 42);
    BOOST_CHECK_THROW(validateMPTFlagMatrix(features), InvalidMPTFlagMatrix);

    // Same with the deprecated flag present: the throw is about the L2 activation block.
    features.set(ledger::Features::Flag::feature_raw_address);
    BOOST_CHECK_THROW(validateMPTFlagMatrix(features), InvalidMPTFlagMatrix);
}

// The deprecation half: setting feature_raw_address is accepted with a warning on every
// entry point (governance setSystemConfig via SystemConfigPrecompiled -> Features::validate,
// config.genesis via NodeConfig::loadGenesisFeatures) — rejecting would fail transactions
// and genesis files written before the deprecation, while the flag is an inert no-op.
BOOST_AUTO_TEST_CASE(DeprecatedRawAddressAcceptedWithWarning)
{
    ledger::Features features;
    BOOST_CHECK_NO_THROW(features.validate(ledger::Features::Flag::feature_raw_address));
    BOOST_CHECK_NO_THROW(features.validate("feature_raw_address"));

    // The name still resolves (the enum value is permanent).
    BOOST_CHECK(ledger::Features::contains("feature_raw_address"));
    BOOST_CHECK(ledger::Features::string2Flag("feature_raw_address") ==
                ledger::Features::Flag::feature_raw_address);

    // isDeprecated is how the entry points know to warn — the genesis-only L2 flag is NOT
    // deprecated: it is a valid genesis feature (and still rejected on the governance path).
    BOOST_CHECK(ledger::Features::isDeprecated(ledger::Features::Flag::feature_raw_address));
    BOOST_CHECK(
        !ledger::Features::isDeprecated(ledger::Features::Flag::feature_l2_ethereum_compat));
    BOOST_CHECK(!ledger::Features::isDeprecated(ledger::Features::Flag::feature_balance));
}

// shouldBuildMPT stays a PURE state-root predicate: the account-table encoding never
// decides whether a block builds an MPT.
BOOST_AUTO_TEST_CASE(ShouldBuildMPT_ScenarioA)
{
    ledger::Features features;
    features.set(ledger::Features::Flag::feature_mpt_state_root);
    features.setActivationBlock(ledger::Features::Flag::feature_mpt_state_root, 100);

    // The activation block N itself stays on XOR (strictly-greater boundary), then MPT.
    BOOST_CHECK(!shouldBuildMPT(features, 99));
    BOOST_CHECK(!shouldBuildMPT(features, 100));
    BOOST_CHECK(shouldBuildMPT(features, 101));
    BOOST_CHECK(shouldBuildMPT(features, 1000));
}

BOOST_AUTO_TEST_CASE(ShouldBuildMPT_ScenarioB)
{
    ledger::Features features;
    features.set(ledger::Features::Flag::feature_l2_ethereum_compat);

    // Scenario B builds from genesis on.
    BOOST_CHECK(shouldBuildMPT(features, 0));
    BOOST_CHECK(shouldBuildMPT(features, 1000));

    // No MPT flag: nothing builds.
    ledger::Features none;
    BOOST_CHECK(!shouldBuildMPT(none, 0));
    BOOST_CHECK(!shouldBuildMPT(none, 1000));
}

BOOST_AUTO_TEST_SUITE_END()
