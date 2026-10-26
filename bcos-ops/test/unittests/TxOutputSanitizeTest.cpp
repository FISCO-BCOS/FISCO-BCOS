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
 * @brief tx's text output carries chain data (receipt message, decoded output): no control byte
 *        of it reaches the terminal
 * @file TxOutputSanitizeTest.cpp
 */
#include "bcos-ops/cmd/TxCmd.h"
#include "bcos-ops/tx/HelloWorld.h"
#include "fixtures/FakeRpc.h"
#include "fixtures/Tty.h"
#include <boost/test/unit_test.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace bcos::ops::test
{
BOOST_AUTO_TEST_SUITE(TxOutputSanitizeTest)

BOOST_AUTO_TEST_CASE(failedCallMessageIsSanitized)
{
    auto dir =
        std::filesystem::temp_directory_path() / ("ops-tx-sanitize-" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir);
    auto abiPath = (dir / "hello.abi").string();
    std::ofstream(abiPath) << helloworld::c_abi;
    FakeRpc rpc;
    Json::Value reverted;
    reverted["status"] = 16;
    reverted["output"] = "0x";
    reverted["message"] = "\x1b]0;owned\x07\x1b[2Jboom";
    rpc.set("call", reverted);
    Connection connection;
    connection.call = rpc.call();
    connection.source = "rpc";
    connection.group = "group0";
    std::ostringstream out;
    int code = 0;
    {
        ScopedTtyStdout tty;
        code = runTxOn(connection, Args({"call", "0xa1", "--abi", abiPath, "get"}, {"json"}), out);
    }
    std::filesystem::remove_all(dir);
    BOOST_CHECK_EQUAL(code, c_exitChecksFailed);
    BOOST_CHECK_EQUAL(out.str(), "call failed, status 16 ]0;owned[2Jboom\n");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
