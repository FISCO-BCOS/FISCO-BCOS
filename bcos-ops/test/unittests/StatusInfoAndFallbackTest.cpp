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
 * @brief status: the informational quorum_connected is not counted as ok, the node-dir PBFT
 *        timeout follows min_seal_time, and --viewchange-window-ms is not a status option
 * @file StatusInfoAndFallbackTest.cpp
 */
#include <bcos-ops/Checks.h>
#include <bcos-ops/Cli.h>
#include <bcos-ops/NodeDir.h>
#include <bcos-ops/OpsError.h>
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
std::filesystem::path nodeDirWith(std::string const& _configIni)
{
    auto dir = std::filesystem::temp_directory_path() /
               ("bcos_ops_fallback_" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "config.ini") << _configIni;
    return dir;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(StatusInfoAndFallbackTest)

BOOST_AUTO_TEST_CASE(timeoutFallbackFollowsMinSealTime)
{
    auto dir = nodeDirWith("[consensus]\n    min_seal_time=8000\n");
    BOOST_CHECK_EQUAL(*NodeDir::load(dir.string()).consensusTimeoutMs, 8001);
    dir = nodeDirWith("[consensus]\n    min_seal_time=500\n");
    BOOST_CHECK_EQUAL(*NodeDir::load(dir.string()).consensusTimeoutMs, 3000);
    std::filesystem::remove_all(dir);
}

// four verdicts ok plus the informational quorum_connected: the summary counts four
BOOST_AUTO_TEST_CASE(summaryDoesNotCountInfoAsOk)
{
    NodeStatus status;
    status.isConsensusNode = true;
    status.latestTimestamp = 1760000000000;
    status.consensusTimeoutMs = 3000;
    status.pendingTxSize = 0;
    status.inTimeout = false;
    status.isSyncing = false;
    status.lag = 0;
    status.connectedGroupNodes = 4;
    status.minRequiredQuorum = 3;
    status.txpoolLimit = 15000;
    std::ostringstream table;
    renderStatus(table, status, evaluate(status, Thresholds{}, 1760000010000), false);
    BOOST_CHECK_MESSAGE(table.str().find("checks     4 ok\n") != std::string::npos, table.str());
}

BOOST_AUTO_TEST_CASE(statusRejectsViewChangeWindow)
{
    std::ostringstream out;
    std::ostringstream err;
    auto code = runOps(
        std::vector<std::string>{"fisco-bcos", "status", "--viewchange-window-ms", "1"}, out, err);
    BOOST_CHECK_EQUAL(code, c_exitUsage);
    BOOST_CHECK_MESSAGE(
        err.str().find("unknown option --viewchange-window-ms") != std::string::npos, err.str());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
