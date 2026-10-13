#include "bcos-framework/ledger/Features.h"
#include "bcos-transaction-scheduler/BaselineSchedulerMPTHelpers.h"
#include <bcos-tool/Exceptions.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::scheduler_v1;

// The MPT state-root predicate no longer involves the account-table
// encoding: the encoding is a node-local physical layout (nodeAddressTableMode), the MPT
// delta scan classifies both layouts (Classify.h parseAccountTable), and the deprecated
// feature_raw_address flag drives nothing — setting it is accepted with a warning. Every
// lane except the legacy v0 lane is mode-aware (the Eth/OP lanes via
// account::ethLaneAccountTableName); only v0 keeps the boot-time hex-only enforcement
// (resolveNodeAddressTableMode).
BOOST_AUTO_TEST_SUITE(RawAddressMPTGuardSuite)

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

    // isDeprecated is how the entry points know to warn. (The old genesis-only L2 flag
    // feature_l2_ethereum_compat is NOT deprecated but REMOVED — its enum value is the
    // tombstone reserved_removed_l2_ethereum_compat and its name no longer resolves — so
    // isDeprecated does not apply to it and there is nothing to assert here; the Ethereum
    // lane is expressed by executor_version >= 2 now.)
    BOOST_CHECK(ledger::Features::isDeprecated(ledger::Features::Flag::feature_raw_address));
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
    BOOST_CHECK(!shouldBuildMPT(0, features, 99));
    BOOST_CHECK(!shouldBuildMPT(0, features, 100));
    BOOST_CHECK(shouldBuildMPT(0, features, 101));
    BOOST_CHECK(shouldBuildMPT(0, features, 1000));
}

BOOST_AUTO_TEST_CASE(ShouldBuildMPT_ScenarioB)
{
    // Scenario B is the Ethereum lane (executor_version >= 2): it builds from genesis on,
    // no feature flag involved.
    ledger::Features features;
    BOOST_CHECK(shouldBuildMPT(ledger::ETHEREUM_EXECUTOR_VERSION, features, 0));
    BOOST_CHECK(shouldBuildMPT(ledger::ETHEREUM_EXECUTOR_VERSION, features, 1000));

    // Legacy lane, no MPT flag: nothing builds.
    ledger::Features none;
    BOOST_CHECK(!shouldBuildMPT(0, none, 0));
    BOOST_CHECK(!shouldBuildMPT(0, none, 1000));
}

BOOST_AUTO_TEST_SUITE_END()
