// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpKarstReleaseGateSuite — Karst cannot ship as a Jovian alias: Osaka + EIP-7825
// exemption. The production parse accepts any known fork as the timestamp-0
// baseline (so `0:karst` is valid on its own); later activations must stay in
// contiguous protocol order, which the gap case below exercises.

#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-framework/ledger/OpForkScheduleCodec.h>
#include <boost/test/unit_test.hpp>

using namespace bcos::evm::opstack;
using bcos::ledger::InvalidOpForkSchedule;

BOOST_AUTO_TEST_SUITE(OpKarstReleaseGateSuite)

BOOST_AUTO_TEST_CASE(KarstConfigIsOsakaWithDepositExemption)
{
    const auto& cfg = karstConfig();
    BOOST_CHECK_EQUAL(cfg.rev, EVMC_OSAKA);
    BOOST_CHECK(cfg.deposit_exempt_from_max_tx_gas);
}

BOOST_AUTO_TEST_CASE(ParseAllowsKarstAfterJovian)
{
    auto schedule = OpForkSchedule::parse("0:jovian,1:karst");
    BOOST_CHECK(schedule.forkAt(0) == OpFork::Jovian);
    BOOST_CHECK(schedule.forkAt(1) == OpFork::Karst);
    BOOST_CHECK_EQUAL(schedule.configAt(1).rev, EVMC_OSAKA);
}

// Rejected by the general contiguity rule (isthmus -> karst skips Fjord/Granite/
// Holocene/Jovian), not by a Karst/Jovian special case.
BOOST_AUTO_TEST_CASE(ParseRejectsSkippedForkBeforeKarst)
{
    BOOST_CHECK_THROW(OpForkSchedule::parse("0:isthmus,1:karst"), InvalidOpForkSchedule);
}

BOOST_AUTO_TEST_SUITE_END()
