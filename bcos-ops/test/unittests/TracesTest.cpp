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
#include <bcos-ops/log/LogParser.h>
#include <bcos-ops/log/Traces.h>
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
}  // namespace

BOOST_AUTO_TEST_SUITE(TracesTest)

BOOST_AUTO_TEST_CASE(viewChangeRoundIsOneRow)
{
    auto trace = traceViewChange(fixtureEvents(), 10);
    BOOST_REQUIRE_EQUAL(trace.rows.size(), 1U);
    auto const& row = trace.rows[0];
    BOOST_CHECK_EQUAL(row[1], "0");                  // fromView
    BOOST_CHECK_EQUAL(row[2], "3");                  // toView
    BOOST_CHECK_EQUAL(row[3], "consensus_timeout");  // reason
    BOOST_CHECK_EQUAL(row[4], "17");                 // waitingIndex
    BOOST_CHECK_EQUAL(row[5], "3000");               // waitedMs
    BOOST_CHECK_EQUAL(row[6], "3/3");                // quorum
    BOOST_CHECK_EQUAL(row[7], "0");                  // newLeader
    BOOST_CHECK(trace.missing.empty());
    auto json = trace.toJson();
    BOOST_CHECK_EQUAL(json["rows"][0]["reason"].asString(), "consensus_timeout");
}

BOOST_AUTO_TEST_CASE(pbftRoundListsInfoEventsInOrder)
{
    auto trace = tracePbft(fixtureEvents(), 19);
    std::vector<std::string> names;
    for (auto const& row : trace.rows)
    {
        names.push_back(row[1]);
    }
    // the node's real order: the checkpoint goes out before ProposalExecuted is printed
    std::vector<std::string> expected = {"PrePrepareReceived", "PrepareQuorum", "CommitQuorum",
        "CheckpointSent", "ProposalExecuted", "CheckpointQuorum", "BlockCommitted", "Report"};
    BOOST_CHECK_EQUAL_COLLECTIONS(names.begin(), names.end(), expected.begin(), expected.end());
    BOOST_CHECK_EQUAL(trace.rows[0][3], "");      // first row has no delta
    BOOST_CHECK_EQUAL(trace.rows[1][3], "+4ms");  // 33.497 -> 33.501
    BOOST_CHECK(trace.rows[0][2].find("fromIdx=2") != std::string::npos);
    BOOST_CHECK(trace.rows[2][2].find("hash=124891bd...") != std::string::npos);
    BOOST_CHECK(trace.rows[7][2].find("roundMs=12") != std::string::npos);
    // the state dump appended to the quorum lines (committedIndex=18, a second view=) does not
    // pull block 18's round in
    BOOST_CHECK(!tracePbft(fixtureEvents(), 18).missing.empty());
    BOOST_CHECK(!tracePbft(fixtureEvents(), 7).missing.empty());
}

BOOST_AUTO_TEST_CASE(txStagesByAbridgedHash)
{
    auto trace = traceTx(
        fixtureEvents(), "0x9c0ffee0123456789abcdef0123456789abcdef0123456789abcdef012345678");
    std::vector<std::string> names;
    for (auto const& row : trace.rows)
    {
        names.push_back(row[1]);
    }
    std::vector<std::string> expected = {"TxAdmitted", "TxSealed", "TxExecuted", "TxRemoved"};
    BOOST_CHECK_EQUAL_COLLECTIONS(names.begin(), names.end(), expected.begin(), expected.end());
    BOOST_CHECK(trace.rows[1][2].find("batchId=19") != std::string::npos);
    BOOST_CHECK(trace.missing.empty());
    auto none = traceTx(fixtureEvents(), "0xdeadbeef");
    BOOST_CHECK(none.rows.empty());
    BOOST_CHECK(none.missing.find("log-level set --module TXPOOL debug") != std::string::npos);
    BOOST_CHECK(none.missing.find("SCHEDULER debug") != std::string::npos);
    BOOST_CHECK(hashMatches("9c0ffee0...", "0x9c0ffee0aaaa"));
    BOOST_CHECK(!hashMatches("9c0ffee0...", "0x9c0ffee1aaaa"));
    BOOST_CHECK(!hashMatches("", "0x9c"));
}

BOOST_AUTO_TEST_CASE(syncSegment)
{
    auto trace = traceSync(fixtureEvents());
    BOOST_REQUIRE_EQUAL(trace.rows.size(), 1U);
    BOOST_CHECK_EQUAL(trace.rows[0][1], "14");
    BOOST_CHECK_EQUAL(trace.rows[0][2], "14");  // SyncFinished prints the number it started at
    BOOST_CHECK_EQUAL(trace.rows[0][3], "0");
    BOOST_CHECK_EQUAL(trace.rows[0][4], "4");
    BOOST_CHECK_EQUAL(trace.rows[0][5], "finished");
}

BOOST_AUTO_TEST_CASE(sealStallSegment)
{
    auto trace = traceSealStall(fixtureEvents());
    BOOST_REQUIRE_EQUAL(trace.rows.size(), 1U);
    BOOST_CHECK_EQUAL(trace.rows[0][2], "1018");  // 31.857147 -> 32.875958
    BOOST_CHECK_EQUAL(trace.rows[0][3], "not_leader");
    BOOST_CHECK_EQUAL(trace.rows[0][4], "17");
    BOOST_CHECK_EQUAL(trace.rows[0][5], "-1");
}

BOOST_AUTO_TEST_CASE(p2pPeerStates)
{
    auto trace = traceP2p(fixtureEvents());
    BOOST_REQUIRE_EQUAL(trace.rows.size(), 3U);
    std::map<std::string, std::string> states;
    for (auto const& row : trace.rows)
    {
        states[row[1]] = row[2];
    }
    // cc77445e left and came back: the later PeerConnected wins
    BOOST_CHECK_EQUAL(states["127.0.0.1:30501"], "connected");
    BOOST_CHECK_EQUAL(states["127.0.0.1:30502"], "connect_failed");
    BOOST_CHECK_EQUAL(states["127.0.0.1:30303"], "handshake_failed");
}

BOOST_AUTO_TEST_CASE(txSyncRound)
{
    auto trace = traceTxSync(fixtureEvents());
    BOOST_REQUIRE_EQUAL(trace.rows.size(), 1U);
    auto const& row = trace.rows[0];
    BOOST_CHECK_EQUAL(row[1], "20");
    BOOST_CHECK_EQUAL(row[2], "1");
    BOOST_CHECK_EQUAL(row[3], "1");
    BOOST_CHECK_EQUAL(row[4], "cc77445e");
    BOOST_CHECK_EQUAL(row[6], "received 1");
    BOOST_CHECK_EQUAL(row[7], "5");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
