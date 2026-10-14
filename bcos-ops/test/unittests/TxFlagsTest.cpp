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
 * @brief tx send/deploy/sign --value --nonce --block-limit reach the signed transaction;
 *        a txpool refusal prints {"error":…} and exits 2; --link calls BFS after a deploy
 * @file TxFlagsTest.cpp
 */
#include "bcos-ops/OpsError.h"
#include "bcos-ops/cmd/TxCmd.h"
#include "bcos-ops/tx/Abi.h"
#include "bcos-ops/tx/HelloWorld.h"
#include "fixtures/FakeRpc.h"
#include <bcos-cpp-sdk/utilities/tx/TransactionBuilder.h>
#include <bcos-protocol/bcos-protocol/TransactionStatus.h>
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
Connection flagsConnection(FakeRpc& _rpc)
{
    Connection connection;
    connection.call = _rpc.call();
    connection.source = "rpc";
    connection.group = "group0";
    connection.endpoint = "127.0.0.1:20200";
    return connection;
}

Args flagsArgs(std::vector<std::string> _argv)
{
    return Args(std::move(_argv), {"json", "yes", "proof", "txs"});
}

Json::Value flagsReceipt(std::string const& _contract = "", std::string const& _output = "0x")
{
    Json::Value receipt;
    receipt["status"] = 0;
    receipt["transactionHash"] = "0x9c";
    receipt["blockNumber"] = 129;
    receipt["gasUsed"] = "21000";
    receipt["contractAddress"] = _contract;
    receipt["output"] = _output;
    return receipt;
}

/// the tars-encoded transaction the command handed to sendTransaction
bcostars::TransactionUniquePtr flagsSentTx(FakeRpc const& _rpc, size_t _nth = 0)
{
    size_t seen = 0;
    for (auto const& [method, params] : _rpc.calls())
    {
        if (method == "sendTransaction" && seen++ == _nth)
        {
            bcos::cppsdk::utilities::TransactionBuilder builder;
            return builder.decodeTransaction(fromHex(params[2].asString()));
        }
    }
    BOOST_FAIL("no sendTransaction call");
    return nullptr;
}

/// per process: ctest runs every case as its own process in parallel, and a shared path lets one
/// case truncate the file while another reads it (an empty ABI fails the encode, not the check)
std::string flagsAbiFile()
{
    auto path = (std::filesystem::temp_directory_path() /
                 ("txflags-hello-" + std::to_string(::getpid()) + ".abi"))
                    .string();
    std::ofstream(path) << helloworld::c_abi;
    return path;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(TxFlagsTest)

BOOST_AUTO_TEST_CASE(nonceAndBlockLimitReachTheSignedTx)
{
    FakeRpc rpc;
    rpc.set("sendTransaction", flagsReceipt());
    std::ostringstream out;
    auto code = runTxOn(flagsConnection(rpc),
        flagsArgs({"send", "0xa1", "--abi", flagsAbiFile(), "set", "hi", "--nonce", "12345",
            "--block-limit", "77", "--json"}),
        out);
    BOOST_CHECK_EQUAL(code, c_exitOk);
    auto tx = flagsSentTx(rpc);
    BOOST_CHECK_EQUAL(tx->data.nonce, "12345");
    BOOST_CHECK_EQUAL(tx->data.blockLimit, 77);
    BOOST_CHECK_EQUAL(tx->data.version, 0);
    BOOST_CHECK_EQUAL(tx->data.to, "0xa1");
    // no getBlockNumber when the limit is explicit
    for (auto const& [method, params] : rpc.calls())
    {
        BOOST_CHECK_NE(method, "getBlockNumber");
    }
}

BOOST_AUTO_TEST_CASE(valueSwitchesToVersion1)
{
    FakeRpc rpc;
    rpc.set("sendTransaction", flagsReceipt());
    std::ostringstream out;
    runTxOn(flagsConnection(rpc),
        flagsArgs({"send", "0xa1", "--abi", flagsAbiFile(), "set", "hi", "--value", "1000"}), out);
    auto tx = flagsSentTx(rpc);
    BOOST_CHECK_EQUAL(tx->data.version, 1);
    BOOST_CHECK_EQUAL(tx->data.value, "0x3e8");         // the node wants hex; 1000 → 0x3e8
    BOOST_CHECK_EQUAL(tx->data.blockLimit, 128 + 500);  // FakeRpc height 128
    BOOST_CHECK(!tx->data.nonce.empty());
    std::ostringstream hexOut;
    runTxOn(flagsConnection(rpc),
        flagsArgs({"send", "0xa1", "--abi", flagsAbiFile(), "set", "hi", "--value", "0x1f"}),
        hexOut);
    BOOST_CHECK_EQUAL(flagsSentTx(rpc, 1)->data.value, "0x1f");
    std::ostringstream bad;
    BOOST_CHECK_THROW(
        runTxOn(flagsConnection(rpc),
            flagsArgs({"send", "0xa1", "--abi", flagsAbiFile(), "set", "hi", "--value", "1e3"}),
            bad),
        OpsError);
}

BOOST_AUTO_TEST_CASE(signPrintsTheRawHexWithoutSending)
{
    FakeRpc rpc;
    std::ostringstream out;
    auto code = runTxOn(flagsConnection(rpc),
        flagsArgs({"sign", "0xa1", "--abi", flagsAbiFile(), "set", "hi", "--json"}), out);
    BOOST_CHECK_EQUAL(code, c_exitOk);
    auto json = parseJson(out.str(), "sign");
    BOOST_CHECK(json["raw"].asString().starts_with("0x"));
    BOOST_CHECK(json["txHash"].asString().starts_with("0x"));
    for (auto const& [method, params] : rpc.calls())
    {
        BOOST_CHECK_NE(method, "sendTransaction");
    }
    bcos::cppsdk::utilities::TransactionBuilder builder;
    auto tx = builder.decodeTransaction(fromHex(json["raw"].asString().substr(2)));
    BOOST_CHECK_EQUAL(tx->data.to, "0xa1");
}

BOOST_AUTO_TEST_CASE(txpoolRefusalIsPrintedAsErrorAndExit2)
{
    FakeRpc rpc;
    rpc.fail("sendTransaction",
        static_cast<int>(bcos::protocol::TransactionStatus::BlockLimitCheckFail),
        "BlockLimitCheckFail");
    std::ostringstream out;
    auto code = runTxOn(flagsConnection(rpc),
        flagsArgs(
            {"send", "0xa1", "--abi", flagsAbiFile(), "set", "hi", "--block-limit", "1", "--json"}),
        out);
    BOOST_CHECK_EQUAL(code, c_exitChecksFailed);
    auto json = parseJson(out.str(), "refusal");
    BOOST_CHECK_EQUAL(json["error"]["code"].asInt(), 10001);
    BOOST_CHECK_EQUAL(json["error"]["message"].asString(), "BlockLimitCheckFail");
    BOOST_CHECK_EQUAL(json["error"]["method"].asString(), "sendTransaction");
    // stdout is not a TTY under the test runner, so the table form is not reachable here
}

BOOST_AUTO_TEST_CASE(deployLinkCallsBfsWithTheNewAddress)
{
    FakeRpc rpc;
    rpc.set("sendTransaction",
        flagsReceipt("0xa1", "0x0000000000000000000000000000000000000000000000000000000000000000"));
    std::ostringstream out;
    auto code = runTxOn(flagsConnection(rpc),
        flagsArgs({"deploy", std::string(helloworld::c_binKeccak), "--abi", flagsAbiFile(),
            "--link", "/apps/hello/v1", "--json"}),
        out);
    BOOST_CHECK_EQUAL(code, c_exitOk);
    auto json = parseJson(out.str(), "deploy");
    BOOST_CHECK_EQUAL(json["link"].asString(), "/apps/hello/v1");
    BOOST_CHECK_EQUAL(json["linkCode"].asInt(), 0);
    auto deployTx = flagsSentTx(rpc, 0);
    BOOST_CHECK_EQUAL(deployTx->data.to, "");
    BOOST_CHECK(!deployTx->data.abi.empty());
    auto linkTx = flagsSentTx(rpc, 1);
    BOOST_CHECK_EQUAL(linkTx->data.to, "0x000000000000000000000000000000000000100e");
    // the CNS form console's `deploy -l` uses: link(name, version, address, abi)
    Json::Value params(Json::arrayValue);
    params.append("hello");
    params.append("v1");
    params.append("0xa1");
    params.append(std::string(helloworld::c_abi));
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    auto expected = Abi(false).encodeMethodBySignature(
        "link(string,string,string,string)", Json::writeString(writer, params));
    BOOST_CHECK_EQUAL(toHex(linkTx->data.input), toHex(expected));
}

BOOST_AUTO_TEST_CASE(valueIsBoundedTo256Bits)
{
    FakeRpc rpc;
    rpc.set("sendTransaction", flagsReceipt());
    std::ostringstream out;
    // 2^256 and 2^256+5 used to wrap to 0x0 / 0x5 through the modular u256
    for (auto const& value :
        {"115792089237316195423570985008687907853269984665640564039457584007913129639936",
            "115792089237316195423570985008687907853269984665640564039457584007913129639941",
            "0x10000000000000000000000000000000000000000000000000000000000000000"})
    {
        BOOST_CHECK_EXCEPTION(
            runTxOn(flagsConnection(rpc),
                flagsArgs({"send", "0xa1", "--abi", flagsAbiFile(), "set", "hi", "--value", value}),
                out),
            OpsError, [](OpsError const& e) {
                return std::string(e.what()).find("256 bits") != std::string::npos;
            });
    }
    BOOST_CHECK(rpc.calls().empty() || std::ranges::none_of(rpc.calls(), [](auto const& c) {
        return c.first == "sendTransaction";
    }));
    runTxOn(flagsConnection(rpc),
        flagsArgs({"send", "0xa1", "--abi", flagsAbiFile(), "set", "hi", "--value", "0X10"}), out);
    BOOST_CHECK_EQUAL(flagsSentTx(rpc, 0)->data.value, "0x10");  // 0X normalised
    runTxOn(flagsConnection(rpc),
        flagsArgs({"send", "0xa1", "--abi", flagsAbiFile(), "set", "hi", "--value",
            "115792089237316195423570985008687907853269984665640564039457584007913129639935"}),
        out);
    BOOST_CHECK_EQUAL(flagsSentTx(rpc, 1)->data.value, "0x" + std::string(64, 'f'));
}

BOOST_AUTO_TEST_CASE(sendWithoutValueKeepsTheVersion0Shape)
{
    FakeRpc rpc;
    rpc.set("sendTransaction", flagsReceipt());
    std::ostringstream out;
    runTxOn(flagsConnection(rpc), flagsArgs({"send", "0xa1", "--abi", flagsAbiFile(), "set", "hi"}),
        out);
    auto tx = flagsSentTx(rpc);
    BOOST_CHECK_EQUAL(tx->data.version, 0);
    BOOST_CHECK(tx->data.value.empty());
    BOOST_CHECK(tx->data.gasPrice.empty());
    BOOST_CHECK_EQUAL(tx->data.gasLimit, 0);
    BOOST_CHECK_EQUAL(tx->data.blockLimit, 128 + 500);
}

BOOST_AUTO_TEST_CASE(linkFailureKeepsTheDeployReceipt)
{
    // the deploy is on chain; the link then returns -53002 FILE_ALREADY_EXIST in its output
    FakeRpc rpc;
    rpc.queue("sendTransaction",
        flagsReceipt("0xa1", "0x0000000000000000000000000000000000000000000000000000000000000000"));
    rpc.queue("sendTransaction",
        flagsReceipt("", "0x00000000000000000000000000000000000000000000000000000000ffff30f6"));
    std::ostringstream out;
    auto code = runTxOn(flagsConnection(rpc),
        flagsArgs({"deploy", std::string(helloworld::c_binKeccak), "--abi", flagsAbiFile(),
            "--link", "/apps/hello/v1", "--json"}),
        out);
    BOOST_CHECK_EQUAL(code, c_exitChecksFailed);
    auto json = parseJson(out.str(), "deploy");
    BOOST_CHECK_EQUAL(json["contractAddress"].asString(), "0xa1");  // not lost
    BOOST_CHECK_EQUAL(json["status"].asInt(), 0);
    BOOST_CHECK_EQUAL(json["linkCode"].asInt(), -53002);
    BOOST_CHECK_EQUAL(json["linkCodeName"].asString(), "FILE_ALREADY_EXIST");

    // the link transaction itself is refused by the txpool: still the deploy receipt + exit 2
    FakeRpc refused;
    refused.queue("sendTransaction",
        flagsReceipt("0xa2", "0x0000000000000000000000000000000000000000000000000000000000000000"));
    refused.fail("sendTransaction",
        static_cast<int>(bcos::protocol::TransactionStatus::TxPoolIsFull), "TxPoolIsFull");
    std::ostringstream out2;
    code = runTxOn(flagsConnection(refused),
        flagsArgs({"deploy", std::string(helloworld::c_binKeccak), "--abi", flagsAbiFile(),
            "--link", "/apps/hello/v1", "--json"}),
        out2);
    BOOST_CHECK_EQUAL(code, c_exitChecksFailed);
    auto json2 = parseJson(out2.str(), "deploy");
    BOOST_CHECK_EQUAL(json2["contractAddress"].asString(), "0xa2");
    BOOST_CHECK(json2["linkError"].asString().find("10002") != std::string::npos);

    // a link receipt without a code word is a failure, never code 0
    FakeRpc empty;
    empty.queue("sendTransaction",
        flagsReceipt("0xa3", "0x0000000000000000000000000000000000000000000000000000000000000000"));
    empty.queue("sendTransaction", flagsReceipt("", "0x"));
    std::ostringstream out3;
    code = runTxOn(flagsConnection(empty),
        flagsArgs({"deploy", std::string(helloworld::c_binKeccak), "--abi", flagsAbiFile(),
            "--link", "/apps/hello/v1", "--json"}),
        out3);
    BOOST_CHECK_EQUAL(code, c_exitChecksFailed);
    BOOST_CHECK(parseJson(out3.str(), "deploy")["linkError"].asString().find("no code") !=
                std::string::npos);
}

BOOST_AUTO_TEST_CASE(deployLinkRejectsRelativePaths)
{
    FakeRpc rpc;
    std::ostringstream out;
    for (auto const& path :
        {"hello", "/apps/hello", "/apps/hello/v1/x", "/apps//v1", "/tables/a/b"})
    {
        BOOST_CHECK_THROW(runTxOn(flagsConnection(rpc),
                              flagsArgs({"deploy", std::string(helloworld::c_binKeccak), "--abi",
                                  flagsAbiFile(), "--link", path}),
                              out),
            OpsError);
    }
    BOOST_CHECK(rpc.calls().empty());  // rejected before any RPC
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
