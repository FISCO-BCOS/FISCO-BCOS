/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-ops/log/LogParser.h>
#include <boost/test/unit_test.hpp>

namespace bcos::ops::test
{
BOOST_AUTO_TEST_SUITE(LogParserTest)

BOOST_AUTO_TEST_CASE(defaultFormatLineGivesEveryField)
{
    auto event = parseLine(
        "info|2026-10-09 14:02:11.031000|io-0x16e293000|[CONSENSUS][PBFT]ViewChangeTriggered,"
        "reason=consensus_timeout,waitingIndex=129,waitedMs=3001,view=3,toView=4");
    BOOST_REQUIRE(event);
    BOOST_CHECK_EQUAL(event->level, "info");
    BOOST_CHECK_EQUAL(event->timestamp, "2026-10-09 14:02:11.031000");
    BOOST_CHECK_EQUAL(event->timeMs % 1000, 31);
    BOOST_REQUIRE_EQUAL(event->badges.size(), 2U);
    BOOST_CHECK_EQUAL(event->badges[0], "CONSENSUS");
    BOOST_CHECK(event->hasBadge("PBFT"));
    BOOST_CHECK_EQUAL(event->name, "ViewChangeTriggered");
    BOOST_CHECK_EQUAL(event->get("reason"), "consensus_timeout");
    BOOST_CHECK_EQUAL(*event->getInt("waitedMs"), 3001);
    BOOST_CHECK(!event->getInt("reason"));
    BOOST_CHECK_EQUAL(event->get("nope", "dflt"), "dflt");
}

BOOST_AUTO_TEST_CASE(decoratorsAreStripped)
{
    auto report = parseLine(
        "info|2026-10-09 "
        "14:02:14.210000|x|[CONSENSUS][PBFT][METRIC]^^^^^^^^Report,committedIndex=129");
    BOOST_REQUIRE(report);
    BOOST_CHECK_EQUAL(report->name, "Report");
    BOOST_CHECK(report->hasBadge("METRIC"));
    auto sent = parseLine(
        "info|2026-10-09 14:02:14.210000|x|[CONSENSUS][PBFT]++++++++++++++++ "
        "PrePrepareSent,index=1,hash=ab");
    BOOST_REQUIRE(sent);
    BOOST_CHECK_EQUAL(sent->name, "PrePrepareSent");
    auto commit = parseLine(
        "info|2026-10-09 14:02:14.210000|x|[CONSENSUS][PBFT]######## CommitQuorum,index=1");
    BOOST_REQUIRE(commit);
    BOOST_CHECK_EQUAL(commit->name, "CommitQuorum");
    BOOST_CHECK_EQUAL(commit->get("index"), "1");
}

BOOST_AUTO_TEST_CASE(blockNumberBadgeBecomesKey)
{
    auto event = parseLine(
        "debug|2026-10-09 14:02:14.210000|x|[SCHEDULER][blk-129]ExecuteBlock request,timeCost=3");
    BOOST_REQUIRE(event);
    BOOST_CHECK_EQUAL(event->get("blk"), "129");
    BOOST_CHECK_EQUAL(event->badges.size(), 1U);
    BOOST_CHECK_EQUAL(event->name, "ExecuteBlock request");
}

BOOST_AUTO_TEST_CASE(nonDefaultPrefixIsSkipped)
{
    BOOST_CHECK(!parseLine("terminate handler called, print stacks"));
    BOOST_CHECK(!parseLine(""));
    BOOST_CHECK(!parseLine("2026-10-09 14:02:14 info [PBFT]Report"));
    BOOST_CHECK(!parseLine("info|not a time|x|[PBFT]Report"));
}

BOOST_AUTO_TEST_CASE(valuesMayContainEquals)
{
    auto event =
        parseLine("info|2026-10-09 14:02:14.210000|x|[TXPOOL]TxRejected,tx=0x12,reason=a=b");
    BOOST_REQUIRE(event);
    BOOST_CHECK_EQUAL(event->get("reason"), "a=b");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
