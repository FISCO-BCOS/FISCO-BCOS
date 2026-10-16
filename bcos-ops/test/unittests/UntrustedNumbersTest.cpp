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
 * @brief numbers from the command line, config.ini and the RPC peer are parsed whole and kept in
 *        range; unknown facts stay unknown
 * @file UntrustedNumbersTest.cpp
 */
#include "fixtures/FakeRpc.h"
#include <bcos-ops/Args.h>
#include <bcos-ops/Checks.h>
#include <bcos-ops/Connect.h>
#include <bcos-ops/NodeDir.h>
#include <bcos-ops/OpsError.h>
#include <bcos-ops/cmd/StatusCmd.h>
#include <bcos-ops/collect/RpcCollector.h>
#include <unistd.h>
#include <boost/test/unit_test.hpp>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>

namespace bcos::ops::test
{
namespace
{
constexpr int64_t c_sampleNowMs = 1760000010000;

Args withOption(std::string _name, std::string _value)
{
    return Args({"--" + _name, std::move(_value)}, {}, std::set<std::string>{_name});
}

NodeStatus behindAndPending()
{
    NodeStatus status;
    status.latestTimestamp = c_sampleNowMs - 1000;
    status.consensusTimeoutMs = 3000;
    status.pendingTxSize = 1;
    status.lag = 50;
    return status;
}

Check const& checkNamed(std::vector<Check> const& _checks, std::string const& _name)
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

BOOST_AUTO_TEST_SUITE(UntrustedNumbersTest)

BOOST_AUTO_TEST_CASE(optionValuesAreParsedWhole)
{
    BOOST_CHECK_EQUAL(withOption("stall-factor", "2.5").numberOr<double>("stall-factor", 2), 2.5);
    BOOST_CHECK_EQUAL(withOption("max-lag", "-5").numberOr<int64_t>("max-lag", 10), -5);
    for (auto const* bad : {"10x", "15s", "nan", "inf", "1e400"})
    {
        BOOST_CHECK_THROW(
            withOption("stall-factor", bad).numberOr<double>("stall-factor", 2), OpsError);
    }
    BOOST_CHECK_THROW(withOption("max-lag", "10x").numberOr<int64_t>("max-lag", 10), OpsError);
    BOOST_CHECK_THROW(withOption("timeout", "3000000000").numberOr<int>("timeout", 1), OpsError);
    BOOST_CHECK_THROW(withOption("count", "-1").numberOr<uint32_t>("count", 1), OpsError);
}

BOOST_AUTO_TEST_CASE(rpcPortIsParsedWhole)
{
    BOOST_CHECK_EQUAL(parseHostPort("127.0.0.1:20200").second, 20200);
    BOOST_CHECK_THROW(parseHostPort("127.0.0.1:20200abc"), OpsError);
    BOOST_CHECK_EQUAL(parseHostPort("http://127.0.0.1:8545/").second, 8545);
}

// a peer reporting an absurd timeout or timestamp cannot overflow the height_advancing arithmetic
BOOST_AUTO_TEST_CASE(extremePeerValuesStayInRange)
{
    auto status = behindAndPending();
    status.consensusTimeoutMs = std::numeric_limits<int64_t>::max();
    auto huge = checkNamed(evaluate(status, Thresholds{}, c_sampleNowMs), "height_advancing");
    BOOST_CHECK(huge.ok());
    BOOST_CHECK_EQUAL(huge.values["limitMs"].asInt64(), std::numeric_limits<int64_t>::max());

    status = behindAndPending();
    status.latestTimestamp = std::numeric_limits<int64_t>::min();
    auto ancient = checkNamed(evaluate(status, Thresholds{}, c_sampleNowMs), "height_advancing");
    BOOST_CHECK(ancient.failed());
    BOOST_CHECK_EQUAL(ancient.values["blockAgeMs"].asInt64(), c_sampleNowMs);

    status.latestTimestamp = std::numeric_limits<int64_t>::max();
    auto future = checkNamed(evaluate(status, Thresholds{}, c_sampleNowMs), "height_advancing");
    BOOST_CHECK(future.ok());
    BOOST_CHECK_EQUAL(future.values["blockAgeMs"].asInt64(), 0);
}

BOOST_AUTO_TEST_CASE(listenPortOutOfRangeIsAnError)
{
    auto dir =
        std::filesystem::temp_directory_path() / ("bcos_ops_port_" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "config.ini") << "[rpc]\n    listen_port=70000\n";
    BOOST_CHECK_THROW(NodeDir::load(dir.string()), OpsError);
    std::filesystem::remove_all(dir);
}

// a NodeStatus with a lag but no isSyncing: the wording says unknown, not "not syncing" / "idle".
// No collector produces one today (lag and isSyncing come from the same getSyncStatus answer);
// the check and the row do not assume that.
BOOST_AUTO_TEST_CASE(unknownSyncStateIsNotPresentedAsIdle)
{
    auto status = behindAndPending();
    auto check = checkNamed(evaluate(status, Thresholds{}, c_sampleNowMs), "sync_caught_up");
    BOOST_CHECK(check.failed());
    BOOST_CHECK_EQUAL(check.detail, "sync state unknown and 50 blocks behind");

    std::ostringstream table;
    renderStatus(table, status, evaluate(status, Thresholds{}, c_sampleNowMs), false);
    BOOST_CHECK_MESSAGE(table.str().find("idle") == std::string::npos, table.str());
}

BOOST_AUTO_TEST_CASE(extremeHeightsAndTimestampsDoNotOverflow)
{
    FakeRpc rpc;
    rpc.set("getBlockNumber", -1);
    rpc.setSync("knownHighestNumber", Json::Int64(std::numeric_limits<int64_t>::max()));
    auto status = collectFromRpc(rpc.call(), "group0", "rpc");
    BOOST_REQUIRE(status.lag);
    BOOST_CHECK_EQUAL(*status.lag, std::numeric_limits<int64_t>::max());

    auto ancient = behindAndPending();
    ancient.latestTimestamp = std::numeric_limits<int64_t>::min();
    std::ostringstream table;
    renderStatus(table, ancient, {}, false);
    BOOST_CHECK_MESSAGE(table.str().find("m ago") != std::string::npos, table.str());
}

BOOST_AUTO_TEST_CASE(peerNumbersAreParsedWhole)
{
    FakeRpc rpc;
    rpc.set("getBlockNumber", "0x80");
    auto status = collectFromRpc(rpc.call(), "group0", "rpc");
    BOOST_CHECK(!status.blockNumber);
    BOOST_CHECK(status.reasons.contains("blockNumber"));
}

// a key iniConfig does not carry stays unknown with a reason instead of becoming "" / false
BOOST_AUTO_TEST_CASE(absentIniConfigKeysStayUnknown)
{
    FakeRpc rpc;
    Json::Value groupInfo;
    groupInfo["chainID"] = "chain0";
    Json::Value node;
    node["nodeID"] = "3a1f00";
    node["iniConfig"] = R"({"chainID":"chain0","nodeID":"3a1f00"})";
    groupInfo["nodeList"].append(node);
    rpc.set("getGroupInfo", groupInfo);
    auto status = collectFromRpc(rpc.call(), "group0", "rpc");
    BOOST_CHECK_EQUAL(*status.chainId, "chain0");
    for (auto const* field : {"version", "smCrypto", "authCheck"})
    {
        BOOST_CHECK_MESSAGE(status.reasons.contains(field), field);
    }
    BOOST_CHECK(!status.version);
    BOOST_CHECK(!status.smCrypto);
    BOOST_CHECK(!status.authCheck);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
