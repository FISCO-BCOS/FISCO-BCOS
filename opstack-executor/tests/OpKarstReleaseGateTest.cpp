// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
/// @file OpKarstReleaseGateTest.cpp
/// @brief Release-gate pins for the Karst fork surface (revision and the EIP-7825 deposit exemption).

// OpKarstReleaseGateSuite — Karst cannot ship as a Jovian alias: Osaka + EIP-7825
// exemption. The production parse accepts any known fork as the timestamp-0
// baseline (so `0:karst` is valid on its own); later activations must stay in
// contiguous protocol order, which the gap case below exercises.

#include <bcos-framework/ledger/OpForkSchedule.h>
#include <bcos-framework/ledger/OpForkScheduleCodec.h>
#include <opstack-executor/OpForkSpec.h>
#include <boost/test/unit_test.hpp>

namespace op = bcos::executor_v1::opstack;
using bcos::ledger::InvalidOpForkSchedule;

BOOST_AUTO_TEST_SUITE(OpKarstReleaseGateSuite)

BOOST_AUTO_TEST_CASE(KarstConfigIsOsakaWithDepositExemption)
{
    const auto& spec = op::OP_KARST_SPEC;
    BOOST_CHECK_EQUAL(spec.rev, EVMC_OSAKA);
    // Deposits are exempt from Karst's EIP-7825 cap: the deposit validate path clamps
    // the revision to Prague (OpEthDeposit.h revValidate) — pin the mechanism.
    BOOST_CHECK_EQUAL(std::min(spec.rev, EVMC_PRAGUE), EVMC_PRAGUE);
}

BOOST_AUTO_TEST_CASE(ParseAllowsKarstAfterJovian)
{
    bcos::ledger::OpForkSchedule schedule;
    schedule.m_jovianTime = 1;
    schedule.m_karstTime = 2;
    BOOST_CHECK(bcos::ledger::resolveOpFork(schedule, 1) == bcos::ledger::OpFork::Jovian);
    BOOST_CHECK(bcos::ledger::resolveOpFork(schedule, 2) == bcos::ledger::OpFork::Karst);
}

// Skipping jovian alone is legal (the shorthand fold produces "isthmus,T:karst"),
// so the contiguity rule now bites one hop earlier: holocene -> karst skips
// isthmus as well and is rejected by the general rule, not a Karst/Jovian special case.
BOOST_AUTO_TEST_CASE(ParseRejectsSkippedForkBeforeKarst)
{
    // karst before jovian
    BOOST_CHECK_THROW(
        std::ignore = bcos::ledger::foldOpForkShorthand(300, 1), InvalidOpForkSchedule);
    // holocene -> karst skips isthmus: rejected by the general contiguity rule (the
    // canonical text, validated by validateScheduleRecords on parse).
    BOOST_CHECK_THROW(
        std::ignore = bcos::ledger::parseOpForkSchedule("0:holocene,100:karst"),
        InvalidOpForkSchedule);
    // the adjacent chain stays legal
    BOOST_CHECK_NO_THROW(
        std::ignore = bcos::ledger::parseOpForkSchedule("0:isthmus,100:karst"));
}

BOOST_AUTO_TEST_SUITE_END()
