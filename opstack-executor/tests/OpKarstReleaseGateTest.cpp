/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file OpKarstReleaseGateTest.cpp
 * @brief Release-gate pins for the Karst fork surface (revision and the EIP-7825 deposit
 *         exemption). */

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

BOOST_AUTO_TEST_CASE(KarstConfigIsOsaka)
{
    const auto& spec = op::OP_KARST_SPEC;
    BOOST_CHECK_EQUAL(spec.rev, EVMC_OSAKA);
    // The deposit exemption from Karst's EIP-7825 cap (revValidate clamping to Prague)
    // is pinned behaviourally by OpEthDepositGasTest::KarstDepositAbove7825CapAdmitted-
    // ButNormalTxRejected — restating the clamp here was arithmetic, not a pin.
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
    BOOST_CHECK_THROW(std::ignore = bcos::ledger::parseOpForkSchedule("0:holocene,100:karst"),
        InvalidOpForkSchedule);
    // the adjacent chain stays legal
    BOOST_CHECK_NO_THROW(std::ignore = bcos::ledger::parseOpForkSchedule("0:isthmus,100:karst"));
}

BOOST_AUTO_TEST_SUITE_END()
