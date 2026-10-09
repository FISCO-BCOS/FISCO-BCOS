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
#include <algorithm>
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
    BOOST_REQUIRE_EQUAL(events.size(), 40U);  // the unprefixed line is skipped
    auto reportMs = reportMsOf(events);
    auto status = collectFromLog(events, reportMs + 5 * 60 * 1000, 60 * 1000);
    BOOST_CHECK_EQUAL(status.source, "log");
    BOOST_CHECK_EQUAL(*status.nodeId, "a5011a8c...");
    BOOST_CHECK_EQUAL(*status.blockNumber, 19);
    BOOST_CHECK_EQUAL(*status.latestHash, "4b49ea15...");
    BOOST_CHECK_EQUAL(*status.latestTimestamp, reportMs);
    BOOST_CHECK_EQUAL(*status.view, 3);
    BOOST_CHECK_EQUAL(*status.changeCycle, 0);
    BOOST_CHECK_EQUAL(*status.consensusTimeoutMs, 3000);
    BOOST_CHECK(status.inTimeout && !*status.inTimeout);  // viewchange was 5 min ago, view==toView
    BOOST_CHECK(status.isConsensusNode && *status.isConsensusNode);  // Idx=0
    BOOST_CHECK_EQUAL(*status.version, "3.18.0");
    BOOST_CHECK(status.isSyncing && !*status.isSyncing);  // SyncFinished is the last flip
    BOOST_CHECK_EQUAL(*status.knownHighestNumber, 14);
    BOOST_CHECK_EQUAL(*status.lag, 0);
    BOOST_CHECK_EQUAL(*status.pendingTxSize, 0);  // the TXPOOL BlockStat, not PBFT's or SYNC's
    BOOST_CHECK(!status.connectedGroupNodes);
    BOOST_CHECK_EQUAL(status.reasons.at("connectedGroupNodes"), "not in log");
    BOOST_CHECK(!status.minRequiredQuorum);
    BOOST_CHECK(!status.peerCount);
    auto json = status.toJson();
    BOOST_CHECK_EQUAL(json["source"].asString(), "log");
    BOOST_CHECK_EQUAL(json["chain"]["blockNumber"].asInt64(), 19);
    BOOST_CHECK(json["consensus"]["connectedGroupNodes"].isNull());
    // checks: quorum skipped (unknown), consensus stable, others computable
    auto checks = evaluate(status, Thresholds{}, reportMs + 5 * 60 * 1000);
    BOOST_CHECK_EQUAL(checks[3].state, "skipped");
    BOOST_CHECK(checks[1].ok());
}

BOOST_AUTO_TEST_CASE(syncStateFollowsTheLatestSyncEvent)
{
    auto events = fixtureEvents();
    auto now = events.back().timeMs + 1000;
    auto isBlockSync = [](Event const& e) { return e.hasBadge("BLOCK SYNC"); };

    // SyncStarted is the latest flip once SyncFinished is gone: syncing, highest/lag from it
    auto started = events;
    started.erase(std::remove_if(started.begin(), started.end(),
                      [](Event const& e) { return e.name == "SyncFinished"; }),
        started.end());
    auto syncing = collectFromLog(started, now, 60 * 1000);
    BOOST_CHECK(syncing.isSyncing && *syncing.isSyncing);
    BOOST_CHECK_EQUAL(*syncing.knownHighestNumber, 16);
    BOOST_CHECK_EQUAL(*syncing.lag, 2);

    // a download longer than the window shows only BlockApplied lines: syncing, highest unknown
    std::vector<Event> progressOnly;
    std::copy_if(events.begin(), events.end(), std::back_inserter(progressOnly),
        [](Event const& e) { return e.name == "BlockApplied"; });
    BOOST_REQUIRE_EQUAL(progressOnly.size(), 2U);
    auto progress = collectFromLog(progressOnly, now, 60 * 1000);
    BOOST_CHECK(progress.isSyncing && *progress.isSyncing);
    BOOST_CHECK(!progress.knownHighestNumber);
    BOOST_CHECK(progress.reasons.at("knownHighestNumber").find("SyncStarted") != std::string::npos);

    // a BlockApplied after the last SyncFinished reopens the segment
    auto reopened = events;
    reopened.push_back(
        *parseLine("info|2026-10-10 02:25:40.000000|io-0x1|[BLOCK SYNC][METRIC]"
                   "[Download]BlockApplied,number=21,hash=00000000...,txsSize=0"));
    BOOST_CHECK(*collectFromLog(reopened, now + 10000, 60 * 1000).isSyncing);

    // no [BLOCK SYNC] line at all: the window cannot tell, and says so
    auto none = events;
    none.erase(std::remove_if(none.begin(), none.end(), isBlockSync), none.end());
    auto unknown = collectFromLog(none, now, 60 * 1000);
    BOOST_CHECK(!unknown.isSyncing);
    BOOST_CHECK(!unknown.knownHighestNumber);
    BOOST_CHECK_EQUAL(unknown.reasons.at("isSyncing"), "no sync event in window");
}

BOOST_AUTO_TEST_CASE(observerPrintsIdxAsUnsignedMax)
{
    // an observer's Report prints Idx=18446744073709551615, which a signed parse rejects
    auto report = *parseLine(
        "info|2026-10-10 02:25:33.511764|io-0x1|[CONSENSUS][PBFT][METRIC]^^^^^^^^Report,sealer=2,"
        "txs=1,committedIndex=19,consNum=20,committedHash=4b49ea15...,view=3,toView=3,"
        "changeCycle=0,expectedCheckPoint=20,Idx=18446744073709551615,sealUntil=0,"
        "waitResealUntil=0,consensusTimeout=3000,nodeId=a5011a8c...,roundMs=12");
    auto status = collectFromLog({report}, report.timeMs, 60 * 1000);
    BOOST_CHECK(status.isConsensusNode && !*status.isConsensusNode);
    BOOST_CHECK_EQUAL(*status.blockNumber, 19);
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
    BOOST_CHECK_EQUAL(status.reasons.at("version"), "no compatibilityVersion line in the window");
    BOOST_CHECK(!status.isSyncing);
    BOOST_CHECK_EQUAL(status.reasons.at("isSyncing"), "no sync event in window");
    auto checks = evaluate(status, Thresholds{}, 0);
    for (auto const& check : checks)
    {
        BOOST_CHECK_EQUAL(check.state, "skipped");
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
