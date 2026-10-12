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
 * @brief tx raw sends the given hex (inline or @file) untouched; tx get --proof asks the node
 *        for both proofs
 * @file TxRawTest.cpp
 */
#include "bcos-ops/OpsError.h"
#include "bcos-ops/cmd/TxCmd.h"
#include "bcos-ops/tx/TxSender.h"
#include "fixtures/FakeRpc.h"
#include <bcos-protocol/bcos-protocol/TransactionStatus.h>
#include <boost/test/unit_test.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace bcos::ops::test
{
namespace
{
Connection rawConnection(FakeRpc& _rpc)
{
    Connection connection;
    connection.call = _rpc.call();
    connection.source = "rpc";
    connection.group = "group0";
    connection.endpoint = "127.0.0.1:20200";
    return connection;
}

Args rawArgs(std::vector<std::string> _argv)
{
    return Args(std::move(_argv), {"json", "yes", "proof", "txs"});
}

Json::Value rawReceipt(std::string const& _contract = "", std::string const& _output = "0x")
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

}  // namespace

BOOST_AUTO_TEST_SUITE(TxRawTest)

BOOST_AUTO_TEST_CASE(rawHexGoesToSendTransactionVerbatim)
{
    FakeRpc rpc;
    rpc.set("sendTransaction", rawReceipt());
    std::ostringstream out;
    auto code = runTxOn(rawConnection(rpc), rawArgs({"raw", "0xdeadbeef", "--json"}), out);
    BOOST_CHECK_EQUAL(code, c_exitOk);
    BOOST_REQUIRE_EQUAL(rpc.calls().size(), 2U);  // getGroupInfo (chain facts) + send
    auto const& [method, params] = rpc.calls().back();
    BOOST_CHECK_EQUAL(method, "sendTransaction");
    BOOST_CHECK_EQUAL(params[0].asString(), "group0");
    BOOST_CHECK_EQUAL(params[2].asString(), "deadbeef");
    BOOST_CHECK_EQUAL(params[3].asBool(), false);
    BOOST_CHECK_EQUAL(parseJson(out.str(), "raw")["transactionHash"].asString(), "0x9c");
}

BOOST_AUTO_TEST_CASE(rawFromFileIsTrimmed)
{
    auto path = (std::filesystem::temp_directory_path() / "txraw-signed.hex").string();
    std::ofstream(path) << "  0xabcd\n\n";
    FakeRpc rpc;
    rpc.set("sendTransaction", rawReceipt());
    std::ostringstream out;
    runTxOn(rawConnection(rpc), rawArgs({"raw", "@" + path}), out);
    BOOST_CHECK_EQUAL(rpc.calls().back().second[2].asString(), "abcd");
    std::ostringstream plain;
    runTxOn(rawConnection(rpc), rawArgs({"raw", path}), plain);  // a bare path works too
    BOOST_CHECK_EQUAL(rpc.calls().back().second[2].asString(), "abcd");
}

BOOST_AUTO_TEST_CASE(rawRejectsNonHexAndMissingFile)
{
    FakeRpc rpc;
    std::ostringstream out;
    BOOST_CHECK_THROW(runTxOn(rawConnection(rpc), rawArgs({"raw", "0xzz"}), out), OpsError);
    BOOST_CHECK_THROW(
        runTxOn(rawConnection(rpc), rawArgs({"raw", "@/no/such/file"}), out), OpsError);
    BOOST_CHECK_THROW(runTxOn(rawConnection(rpc), rawArgs({"raw"}), out), OpsError);
}

BOOST_AUTO_TEST_CASE(rawRefusalExits2)
{
    FakeRpc rpc;
    rpc.fail("sendTransaction", static_cast<int>(bcos::protocol::TransactionStatus::TxPoolIsFull),
        "TxPoolIsFull");
    std::ostringstream out;
    auto code = runTxOn(rawConnection(rpc), rawArgs({"raw", "0x01", "--json"}), out);
    BOOST_CHECK_EQUAL(code, c_exitChecksFailed);
    BOOST_CHECK_EQUAL(parseJson(out.str(), "raw")["error"]["code"].asInt(), 10002);
}

BOOST_AUTO_TEST_CASE(getProofPassesTrueToBothQueries)
{
    FakeRpc rpc;
    Json::Value tx;
    tx["hash"] = "0x9c";
    tx["from"] = "0xf1";
    tx["to"] = "0xa1";
    tx["txProof"] = Json::Value(Json::arrayValue);
    tx["txProof"].append("0x01");
    tx["txProof"].append("0x02");
    rpc.set("getTransaction", tx);
    auto receipt = rawReceipt();
    receipt["txReceiptProof"] = Json::Value(Json::arrayValue);
    receipt["txReceiptProof"].append("0x03");
    rpc.set("getTransactionReceipt", receipt);
    std::ostringstream out;
    auto code = runTxOn(rawConnection(rpc), rawArgs({"get", "0x9c", "--proof", "--json"}), out);
    BOOST_CHECK_EQUAL(code, c_exitOk);
    for (auto const& [method, params] : rpc.calls())
    {
        if (method == "getTransaction" || method == "getTransactionReceipt")
        {
            BOOST_CHECK_EQUAL(params[2].asString(), "0x9c");
            BOOST_CHECK_EQUAL(params[3].asBool(), true);
        }
    }
    auto json = parseJson(out.str(), "get");
    BOOST_CHECK_EQUAL(json["transaction"]["txProof"].size(), 2U);
    BOOST_CHECK_EQUAL(json["receipt"]["txReceiptProof"].size(), 1U);
    std::ostringstream noProof;
    runTxOn(rawConnection(rpc), rawArgs({"get", "0x9c"}), noProof);
    BOOST_CHECK_EQUAL(rpc.calls().back().second[3].asBool(), false);
}

BOOST_AUTO_TEST_CASE(rawHexIsNormalizedTheSameWayEverywhere)
{
    // a multi-line paste (the TUI's raw box) and the CLI share normalizeRawTxHex
    BOOST_CHECK_EQUAL(normalizeRawTxHex("0xAB\ncd \r\n ef\t"), "ABcdef");
    BOOST_CHECK_EQUAL(normalizeRawTxHex("  0X10  "), "10");
    BOOST_CHECK_THROW(normalizeRawTxHex("0xabc"), OpsError);  // odd length
    BOOST_CHECK_THROW(normalizeRawTxHex("zz"), OpsError);     // not hex
    BOOST_CHECK_THROW(normalizeRawTxHex("  \n "), OpsError);  // nothing left
    auto path = (std::filesystem::temp_directory_path() / "txraw-multiline.hex").string();
    std::ofstream(path) << "0xab\ncd\n";
    FakeRpc rpc;
    rpc.set("sendTransaction", rawReceipt());
    std::ostringstream out;
    runTxOn(rawConnection(rpc), rawArgs({"raw", "@" + path}), out);
    BOOST_CHECK_EQUAL(rpc.calls().back().second[2].asString(), "abcd");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
