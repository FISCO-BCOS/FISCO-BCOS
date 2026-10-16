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
 * @brief every option a command's code reads is one the strict parser accepts for it: each name
 *        below comes from an option()/optionOr()/numberOr() read on that command's path
 *        (helpers included: connectOptionsFrom, thresholdsFrom, TxContext's makeSender /
 *        routingFor). `--help` ends the run right after parsing, so nothing connects.
 * @file CommandOptionsTest.cpp
 */
#include <bcos-ops/Cli.h>
#include <bcos-ops/OpsError.h>
#include <boost/test/unit_test.hpp>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace bcos::ops::test
{
BOOST_AUTO_TEST_SUITE(CommandOptionsTest)

BOOST_AUTO_TEST_CASE(everyOptionACommandReadsIsAccepted)
{
    std::vector<std::string> const connect = {
        "node-dir", "rpc", "connect-timeout", "timeout", "mode", "web3"};
    std::vector<std::string> const preset = {"account", "interval"};
    std::map<std::string, std::vector<std::string>> reads = {
        {"status", {"stall-factor", "max-lag"}},  // status reads no log: no view-change window
        {"log", {"since", "last", "stall-factor", "max-lag", "viewchange-window-ms"}},
        {"tui", {"account", "key", "filter", "snapshot-dir", "stall-factor", "max-lag",
                    "viewchange-window-ms"}},
        {"tx", {"account", "abi", "value", "link", "nonce", "block-limit", "wait"}},
        {"account", {"out"}},
        {"amop", {"count", "wait"}},
        {"attach", {}},
        {"log-level", {"module"}},
        {"block", {}},
        {"auth", preset},
        {"balance", preset},
        {"sysconfig", preset},
        {"committee", {"account", "interval", "last"}},
        {"consensus", {"account", "interval", "term-weight"}},
    };
    BOOST_CHECK_EQUAL(reads.size(), commandTable().size());  // a new command joins this table
    for (auto& [command, names] : reads)
    {
        BOOST_REQUIRE_MESSAGE(commandTable().contains(command), command);
        names.insert(names.end(), connect.begin(), connect.end());
        for (auto const& name : names)
        {
            std::ostringstream out;
            std::ostringstream err;
            auto code = runOps({"fisco-bcos", command, "--" + name, "1", "--help"}, out, err);
            BOOST_CHECK_MESSAGE(code == c_exitOk,
                command + " --" + name + ": exit " + std::to_string(code) + " " + err.str());
        }
        std::ostringstream out;
        std::ostringstream err;
        BOOST_CHECK_EQUAL(
            runOps({"fisco-bcos", command, "--no-such-option", "1"}, out, err), c_exitUsage);
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
