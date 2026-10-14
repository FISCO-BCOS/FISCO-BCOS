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
 * @brief `log status` follows status's exit rule: a log that yields no check is exit 2
 * @file LogStatusCmdTest.cpp
 */
#include <bcos-ops/Cli.h>
#include <bcos-ops/OpsError.h>
#include <unistd.h>
#include <boost/test/unit_test.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace bcos::ops::test
{
BOOST_AUTO_TEST_SUITE(LogStatusCmdTest)

BOOST_AUTO_TEST_CASE(logWithoutAnyCheckableEventIsNotHealthy)
{
    auto dir = std::filesystem::temp_directory_path() /
               ("bcos_ops_logstatus_" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir / "log");
    std::ofstream(dir / "config.ini") << "[log]\n    log_path=log\n";
    std::ofstream(dir / "log" / "log_2026101001.44.log")
        << "info|2026-10-10 01:44:00.000000|main|[INITIALIZER]start the node\n";

    std::ostringstream out;
    std::ostringstream err;
    auto code = runOps(
        {"fisco-bcos", "log", "status", "--node-dir", dir.string(), "--since", "100000h", "--json"},
        out, err);
    std::filesystem::remove_all(dir);
    BOOST_CHECK_MESSAGE(code == c_exitChecksFailed, "exit " << code << " " << err.str());
    BOOST_CHECK(out.str().find("\"state\" : \"ok\"") == std::string::npos);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
