/*
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
 * @brief status when nothing could be collected, remote text in the reasons block, and a node
 *        directory whose config.genesis cannot be read
 * @file StatusEdgeCasesTest.cpp
 */
#include "fixtures/FakeRpc.h"
#include <bcos-ops/Connect.h>
#include <bcos-ops/NodeDir.h>
#include <bcos-ops/cmd/StatusCmd.h>
#include <unistd.h>
#include <boost/test/unit_test.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace bcos::ops::test
{
namespace
{
Connection connectionOf(RpcCall _call)
{
    Connection connection;
    connection.call = std::move(_call);
    connection.source = "rpc";
    connection.group = "group0";
    connection.endpoint = "127.0.0.1:20200";
    return connection;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(StatusEdgeCasesTest)

// getGroupList answers, every group-scoped call fails (node starting, group services down)
BOOST_AUTO_TEST_CASE(nothingEvaluatedIsNotHealthy)
{
    FakeRpc rpc;
    for (auto const* method : {"getConsensusStatus", "getSyncStatus", "getGroupInfo",
             "getBlockNumber", "getBlockByNumber", "getPendingTxSize"})
    {
        rpc.remove(method);
    }
    std::ostringstream table;
    BOOST_CHECK_EQUAL(
        runStatusOn(connectionOf(rpc.call()), Thresholds{}, false, table), c_exitChecksFailed);
    BOOST_CHECK(table.str().find("none evaluated  (5 skipped)") != std::string::npos);
    std::ostringstream json;
    BOOST_CHECK_EQUAL(
        runStatusOn(connectionOf(rpc.call()), Thresholds{}, true, json), c_exitChecksFailed);
    for (auto const& check : parseJson(json.str(), "status")["checks"])
    {
        BOOST_CHECK_EQUAL(check["state"].asString(), "skipped");
    }
}

BOOST_AUTO_TEST_CASE(reasonsAreSanitized)
{
    RpcCall hostile = [](std::string_view _method, Json::Value const&) -> Json::Value {
        throw OpsError(c_exitUsage, std::string(_method) + " failed: \x1b]0;owned\x07\x1b[2J");
    };
    std::ostringstream out;
    runStatusOn(connectionOf(hostile), Thresholds{}, false, out);
    BOOST_CHECK(out.str().find("failed: ]0;owned[2J") != std::string::npos);
    BOOST_CHECK(out.str().find('\x1b') == std::string::npos);
    BOOST_CHECK(out.str().find('\x07') == std::string::npos);
}

BOOST_AUTO_TEST_CASE(unreadableGenesisIsUsageError)
{
    auto dir = std::filesystem::temp_directory_path() /
               ("bcos_ops_badgenesis_" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "config.ini") << "[rpc]\n    listen_port=20200\n";
    std::ofstream(dir / "config.genesis") << "[chain\n    group_id=group9\n";
    try
    {
        NodeDir::load(dir.string());
        BOOST_ERROR("NodeDir::load accepted an unreadable config.genesis");
    }
    catch (OpsError const& e)
    {
        BOOST_CHECK_EQUAL(e.exitCode, c_exitUsage);
        BOOST_CHECK(std::string(e.what()).find("config.genesis") != std::string::npos);
    }
    std::filesystem::remove_all(dir);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
