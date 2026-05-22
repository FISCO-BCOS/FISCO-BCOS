/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief FC-R*: schedule / toRevision() forward-compatibility mapping.
 *  @file CompatRevisionTest.cpp
 */

#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/protocol/Protocol.h"
#include "vm/VMInstance.h"
#include <boost/test/unit_test.hpp>

namespace bcos::test
{

BOOST_AUTO_TEST_SUITE(Compat)
BOOST_AUTO_TEST_SUITE(CompatRevision)

BOOST_AUTO_TEST_CASE(FC_R_cancun_without_prague)
{
    using namespace bcos::executor;

    BOOST_CHECK_EQUAL(toRevision(FiscoBcosScheduleCancun), EVMC_CANCUN);
    BOOST_CHECK(FiscoBcosScheduleCancun.enableCanCun);
    BOOST_CHECK(!FiscoBcosScheduleCancun.enablePrague);
    BOOST_CHECK(!FiscoBcosScheduleCancun.enableOsaka);
}

BOOST_AUTO_TEST_CASE(FC_R_london_without_cancun)
{
    using namespace bcos::executor;

    BOOST_CHECK_EQUAL(toRevision(FiscoBcosSchedule), EVMC_LONDON);
    BOOST_CHECK(!FiscoBcosSchedule.enableCanCun);
    BOOST_CHECK(!FiscoBcosSchedule.enablePrague);
    BOOST_CHECK(!FiscoBcosSchedule.enableOsaka);
}

BOOST_AUTO_TEST_CASE(FC_R_shanghai_via_pairs)
{
    using namespace bcos::executor;

    // V3.2+ without Cancun: Ethereum Shanghai (pre-Cancun last fork, EIP-3855 PUSH0).
    BOOST_CHECK_EQUAL(toRevision(FiscoBcosScheduleV320), EVMC_SHANGHAI);
    BOOST_CHECK(FiscoBcosScheduleV320.enablePairs);
    BOOST_CHECK(!FiscoBcosScheduleV320.enableCanCun);
    BOOST_CHECK(!FiscoBcosScheduleV320.enablePrague);
    BOOST_CHECK(!FiscoBcosScheduleV320.enableOsaka);
}

BOOST_AUTO_TEST_CASE(FC_R_revision_priority_order)
{
    using namespace bcos::executor;

    BOOST_CHECK_EQUAL(toRevision(FiscoBcosScheduleOsaka), EVMC_OSAKA);
    BOOST_CHECK_EQUAL(toRevision(FiscoBcosSchedulePrague), EVMC_PRAGUE);
    BOOST_CHECK_EQUAL(toRevision(FiscoBcosScheduleCancun), EVMC_CANCUN);
    BOOST_CHECK_EQUAL(toRevision(FiscoBcosScheduleV320), EVMC_SHANGHAI);
    BOOST_CHECK_EQUAL(toRevision(FiscoBcosSchedule), EVMC_LONDON);
}

BOOST_AUTO_TEST_CASE(FC_R_features_block_version_ladder)
{
    using namespace bcos::executor;
    using bcos::protocol::BlockVersion;

    ledger::Features f;
    BOOST_CHECK_EQUAL(
        toRevision(f, static_cast<uint32_t>(BlockVersion::V3_0_VERSION)), EVMC_LONDON);

    BOOST_CHECK_EQUAL(
        toRevision(f, static_cast<uint32_t>(BlockVersion::V3_2_VERSION)), EVMC_SHANGHAI);

    f.set(ledger::Features::Flag::feature_evm_cancun);
    BOOST_CHECK_EQUAL(
        toRevision(f, static_cast<uint32_t>(BlockVersion::V3_2_VERSION)), EVMC_CANCUN);

    f.set(ledger::Features::Flag::feature_evm_prague);
    BOOST_CHECK_EQUAL(
        toRevision(f, static_cast<uint32_t>(BlockVersion::V3_2_VERSION)), EVMC_PRAGUE);

    f.set(ledger::Features::Flag::feature_evm_osaka);
    BOOST_CHECK_EQUAL(toRevision(f, static_cast<uint32_t>(BlockVersion::V3_2_VERSION)), EVMC_OSAKA);
}

BOOST_AUTO_TEST_SUITE_END()  // CompatRevision
BOOST_AUTO_TEST_SUITE_END()  // Compat

}  // namespace bcos::test
