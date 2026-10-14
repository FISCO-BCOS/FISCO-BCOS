/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-ops/Checks.h>
#include <boost/test/unit_test.hpp>

namespace bcos::ops::test
{
namespace
{
constexpr int64_t c_now = 1760000010000;  // 10 s after the sample block

NodeStatus healthy()
{
    NodeStatus status;
    status.isConsensusNode = true;
    status.latestTimestamp = 1760000000000;
    status.consensusTimeoutMs = 3000;
    status.pendingTxSize = 0;
    status.inTimeout = false;
    status.isSyncing = false;
    status.lag = 0;
    status.connectedGroupNodes = 4;  // self + 3 peers
    status.minRequiredQuorum = 3;
    status.txpoolLimit = 15000;
    return status;
}

Check const& find(std::vector<Check> const& _checks, std::string const& _name)
{
    for (auto const& check : _checks)
    {
        if (check.name == _name)
        {
            return check;
        }
    }
    throw std::runtime_error("no check " + _name);
}
}  // namespace

BOOST_AUTO_TEST_SUITE(ChecksTest)

BOOST_AUTO_TEST_CASE(healthyNodePassesAllFive)
{
    auto checks = evaluate(healthy(), Thresholds{}, c_now);
    BOOST_REQUIRE_EQUAL(checks.size(), 5U);
    for (auto const& check : checks)
    {
        BOOST_CHECK_MESSAGE(check.ok(), check.name + " " + check.state + " " + check.detail);
    }
    BOOST_CHECK(!anyFailed(checks));
    auto json = checksToJson(checks);
    BOOST_CHECK_EQUAL(json.size(), 5U);
    BOOST_CHECK_EQUAL(json[1]["name"].asString(), "consensus_stable");
    BOOST_CHECK(json[1]["ok"].asBool());
    BOOST_CHECK(!json[1]["inTimeout"].asBool());
}

BOOST_AUTO_TEST_CASE(heightAdvancing)
{
    auto status = healthy();
    status.pendingTxSize = 5;  // 10 s old block, 2 × 3 s limit → stalled
    auto checks = evaluate(status, Thresholds{}, c_now);
    BOOST_CHECK(find(checks, "height_advancing").failed());
    // no pending txs: an old block is fine
    status.pendingTxSize = 0;
    BOOST_CHECK(find(evaluate(status, Thresholds{}, c_now), "height_advancing").ok());
    // a larger --stall-factor accepts the age
    status.pendingTxSize = 5;
    Thresholds lenient;
    lenient.stallFactor = 10;
    BOOST_CHECK(find(evaluate(status, lenient, c_now), "height_advancing").ok());
}

BOOST_AUTO_TEST_CASE(consensusStable)
{
    auto status = healthy();
    status.inTimeout = true;
    auto checks = evaluate(status, Thresholds{}, c_now);
    BOOST_CHECK(find(checks, "consensus_stable").failed());
    BOOST_CHECK(anyFailed(checks));
    BOOST_CHECK(checksToJson(checks)[1]["inTimeout"].asBool());
}

BOOST_AUTO_TEST_CASE(syncCaughtUp)
{
    auto status = healthy();
    status.isSyncing = true;
    status.lag = 10;
    BOOST_CHECK(find(evaluate(status, Thresholds{}, c_now), "sync_caught_up").failed());
    status.lag = 9;
    BOOST_CHECK(find(evaluate(status, Thresholds{}, c_now), "sync_caught_up").ok());
    status.lag = 50;
    status.isSyncing = false;  // behind but not syncing: height check owns that case
    BOOST_CHECK(find(evaluate(status, Thresholds{}, c_now), "sync_caught_up").ok());
    Thresholds strict;
    strict.maxLag = 1;
    status.isSyncing = true;
    status.lag = 1;
    BOOST_CHECK(find(evaluate(status, strict, c_now), "sync_caught_up").failed());
}

BOOST_AUTO_TEST_CASE(quorumConnected)
{
    auto status = healthy();
    status.connectedGroupNodes = 2;  // self + 1 peer < quorum 3
    BOOST_CHECK(find(evaluate(status, Thresholds{}, c_now), "quorum_connected").failed());
    status.connectedGroupNodes = 3;  // self + 2 peers = quorum 3
    BOOST_CHECK(find(evaluate(status, Thresholds{}, c_now), "quorum_connected").ok());
    status.isConsensusNode = false;
    BOOST_CHECK_EQUAL(
        find(evaluate(status, Thresholds{}, c_now), "quorum_connected").state, "skipped");
}

BOOST_AUTO_TEST_CASE(txpoolNotFull)
{
    auto status = healthy();
    status.pendingTxSize = 15000;
    BOOST_CHECK(find(evaluate(status, Thresholds{}, c_now), "txpool_not_full").failed());
    status.pendingTxSize = 14999;
    BOOST_CHECK(find(evaluate(status, Thresholds{}, c_now), "txpool_not_full").ok());
}

BOOST_AUTO_TEST_CASE(nullInputsSkipNotFail)
{
    NodeStatus empty;
    empty.source = "log";
    auto checks = evaluate(empty, Thresholds{}, c_now);
    BOOST_REQUIRE_EQUAL(checks.size(), 5U);
    for (auto const& check : checks)
    {
        BOOST_CHECK_EQUAL(check.state, "skipped");
    }
    BOOST_CHECK(!anyFailed(checks));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
