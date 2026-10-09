/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include "fixtures/Events.h"
#include <bcos-ops/Checks.h>
#include <bcos-ops/collect/LogCollector.h>
#include <bcos-ops/log/LogParser.h>
#include <boost/test/unit_test.hpp>
#include <sstream>

namespace bcos::ops::test
{
namespace
{
std::vector<Event> fixtureEvents()
{
    std::vector<Event> events;
    std::istringstream in(c_eventsLog);
    std::string line;
    while (std::getline(in, line))
    {
        if (auto event = parseLine(line))
        {
            events.push_back(*event);
        }
    }
    return events;
}

int64_t reportMsOf(std::vector<Event> const& _events)
{
    for (auto const& event : _events)
    {
        if (event.name == "Report")
        {
            return event.timeMs;
        }
    }
    throw std::runtime_error("fixture has no Report");
}
}  // namespace

BOOST_AUTO_TEST_SUITE(LogCollectorTest)

BOOST_AUTO_TEST_CASE(fieldsComeFromReportSyncAndBlockStat)
{
    auto events = fixtureEvents();
    BOOST_REQUIRE_EQUAL(events.size(), 31U);  // the unprefixed line is skipped
    auto reportMs = reportMsOf(events);
    auto status = collectFromLog(events, reportMs + 5 * 60 * 1000, 60 * 1000);
    BOOST_CHECK_EQUAL(status.source, "log");
    BOOST_CHECK_EQUAL(*status.nodeId, "3a1f00aa...");
    BOOST_CHECK_EQUAL(*status.blockNumber, 129);
    BOOST_CHECK_EQUAL(*status.latestHash, "85691626...");
    BOOST_CHECK_EQUAL(*status.latestTimestamp, reportMs);
    BOOST_CHECK_EQUAL(*status.view, 4);
    BOOST_CHECK_EQUAL(*status.changeCycle, 0);
    BOOST_CHECK_EQUAL(*status.consensusTimeoutMs, 3000);
    BOOST_CHECK(status.inTimeout && !*status.inTimeout);  // viewchange was 5 min ago, view==toView
    BOOST_CHECK(status.isConsensusNode && *status.isConsensusNode);
    BOOST_CHECK_EQUAL(*status.version, "3.18.0");
    BOOST_CHECK(status.isSyncing && !*status.isSyncing);  // SyncFinished is the last flip
    BOOST_CHECK_EQUAL(*status.knownHighestNumber, 140);
    BOOST_CHECK_EQUAL(*status.lag, 0);
    BOOST_CHECK_EQUAL(*status.pendingTxSize, 3);
    BOOST_CHECK(!status.connectedGroupNodes);
    BOOST_CHECK_EQUAL(status.reasons.at("connectedGroupNodes"), "not in log");
    BOOST_CHECK(!status.minRequiredQuorum);
    BOOST_CHECK(!status.peerCount);
    auto json = status.toJson();
    BOOST_CHECK_EQUAL(json["source"].asString(), "log");
    BOOST_CHECK_EQUAL(json["chain"]["blockNumber"].asInt64(), 129);
    BOOST_CHECK(json["consensus"]["connectedGroupNodes"].isNull());
    // checks: quorum skipped (unknown), consensus stable, others computable
    auto checks = evaluate(status, Thresholds{}, reportMs + 5 * 60 * 1000);
    BOOST_CHECK_EQUAL(checks[3].state, "skipped");
    BOOST_CHECK(checks[1].ok());
}

BOOST_AUTO_TEST_CASE(recentViewChangeMarksUnstable)
{
    auto events = fixtureEvents();
    auto reportMs = reportMsOf(events);
    auto status = collectFromLog(events, reportMs + 10 * 1000, 60 * 1000);
    BOOST_CHECK(*status.inTimeout);
    auto checks = evaluate(status, Thresholds{}, reportMs + 10 * 1000);
    BOOST_CHECK(checks[1].failed());
}

BOOST_AUTO_TEST_CASE(missingBlockStatLeavesPendingNull)
{
    auto events = fixtureEvents();
    events.erase(std::remove_if(events.begin(), events.end(),
                     [](Event const& e) { return e.name == "BlockStat"; }),
        events.end());
    auto status = collectFromLog(events, events.back().timeMs, 60 * 1000);
    BOOST_CHECK(!status.pendingTxSize);
    BOOST_CHECK(status.reasons.at("pendingTxSize").find("BlockStat") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(noReportGivesNullsWithReason)
{
    std::vector<Event> none;
    auto status = collectFromLog(none, 0, 60 * 1000);
    BOOST_CHECK(!status.blockNumber);
    BOOST_CHECK_EQUAL(status.reasons.at("blockNumber"), "no Report line in the window");
    BOOST_CHECK(!status.version);
    auto checks = evaluate(status, Thresholds{}, 0);
    for (auto const& check : checks)
    {
        BOOST_CHECK_EQUAL(check.state, "skipped");
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
