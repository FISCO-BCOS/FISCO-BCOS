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
 * @brief a deploy with an ABI that has no constructor entry (solc omits the implicit one)
 *        sends the bytecode alone; constructor arguments against such an ABI are refused
 * @file DeployAbiTest.cpp
 */
#include "bcos-ops/OpsError.h"
#include "bcos-ops/cmd/TxCmd.h"
#include "bcos-ops/tx/Abi.h"
#include "bcos-ops/tx/HelloWorld.h"
#include "fixtures/FakeRpc.h"
#include <bcos-utilities/DataConvertUtility.h>
#include <unistd.h>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace bcos::ops::test
{
namespace
{
// HelloWorld's ABI without its constructor entry
std::string const c_abiNoCtor =
    R"([{"inputs":[],"name":"get","outputs":[{"internalType":"string","name":"","type":"string"}],"stateMutability":"view","type":"function"},)"
    R"({"inputs":[{"internalType":"string","name":"n","type":"string"}],"name":"set","outputs":[],"stateMutability":"nonpayable","type":"function"}])";
}  // namespace

BOOST_AUTO_TEST_SUITE(DeployAbiTest)

BOOST_AUTO_TEST_CASE(abiWithoutConstructorDeploysTheBytecodeAlone)
{
    Abi abi(false);
    auto bin = std::string(helloworld::binary(false));
    auto data = abi.encodeConstructor(c_abiNoCtor, bin, "[]");
    BOOST_CHECK(data == fromHex(bin));
    // an argument-less constructor entry adds nothing after the bytecode either
    auto withCtor = abi.encodeConstructor(std::string(helloworld::c_abi), bin, "[]");
    BOOST_CHECK(withCtor == data);
    BOOST_CHECK_EXCEPTION(
        abi.encodeConstructor(c_abiNoCtor, bin, R"(["x"])"), OpsError, [](OpsError const& e) {
            return e.exitCode == c_exitUsage &&
                   std::string(e.what()).find("no constructor") != std::string::npos;
        });
}

BOOST_AUTO_TEST_CASE(txDeployWithSuchAnAbiSendsTheBytecode)
{
    auto dir = std::filesystem::temp_directory_path() /
               ("ops-deploy-abi-test-" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir);
    auto abiPath = (dir / "noctor.abi").string();
    std::ofstream(abiPath) << c_abiNoCtor;
    FakeRpc rpc;
    Json::Value receipt;
    receipt["status"] = 0;
    receipt["transactionHash"] = "0x9c";
    receipt["blockNumber"] = 129;
    receipt["gasUsed"] = "21000";
    receipt["contractAddress"] = "0xa1";
    receipt["output"] = "0x";
    rpc.set("sendTransaction", receipt);
    Connection connection;
    connection.call = rpc.call();
    connection.source = "rpc";
    connection.group = "group0";
    std::ostringstream out;
    auto code = runTxOn(connection,
        Args({"deploy", std::string(helloworld::binary(false)), "--abi", abiPath, "--json"},
            {"json"}),
        out);
    BOOST_CHECK_EQUAL(code, c_exitOk);
    BOOST_CHECK(out.str().find("\"contractAddress\"") != std::string::npos);
    bool sent = false;
    for (auto const& [method, params] : rpc.calls())
    {
        sent = sent || method == "sendTransaction";
    }
    BOOST_CHECK(sent);
    std::filesystem::remove_all(dir);
}

BOOST_AUTO_TEST_CASE(constructorArgumentsFollowTheBytecodeWithoutASelector)
{
    // constructor(int256 a, string s): abi.encode(7, "init s") =
    //   0x07 | offset 0x40 | len 6 | "init s" right-padded — no 4-byte selector in front
    std::string const abiText =
        R"([{"inputs":[{"name":"a","type":"int256"},{"name":"s","type":"string"}],"stateMutability":"nonpayable","type":"constructor"}])";
    std::string const expectedArgs =
        "0000000000000000000000000000000000000000000000000000000000000007"
        "0000000000000000000000000000000000000000000000000000000000000040"
        "0000000000000000000000000000000000000000000000000000000000000006"
        "696e697420730000000000000000000000000000000000000000000000000000";
    for (bool sm : {false, true})
    {
        auto data = Abi(sm).encodeConstructor(abiText, "6080", R"([7,"init s"])");
        BOOST_CHECK_EQUAL(toHex(data), "6080" + expectedArgs);
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
