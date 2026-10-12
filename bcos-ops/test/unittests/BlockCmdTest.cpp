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
 * @brief block get picks getBlockByNumber / getBlockByHash, --txs flips the two flags, --proof
 *        attaches per-transaction proofs
 * @file BlockCmdTest.cpp
 */
#include "bcos-ops/OpsError.h"
#include "bcos-ops/cmd/TxCmd.h"
#include "fixtures/FakeRpc.h"
#include <boost/test/unit_test.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace bcos::ops::test
{
namespace
{
Connection blockConnection(FakeRpc& _rpc)
{
    Connection connection;
    connection.call = _rpc.call();
    connection.source = "rpc";
    connection.group = "group0";
    connection.endpoint = "127.0.0.1:20200";
    return connection;
}

Args blockArgs(std::vector<std::string> _argv)
{
    return Args(std::move(_argv), {"json", "yes", "proof", "txs"});
}

Json::Value blockReceipt(std::string const& _contract = "", std::string const& _output = "0x")
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

namespace
{
Json::Value sampleBlock()
{
    Json::Value block;
    block["number"] = 5;
    block["hash"] = "0x" + std::string(64, 'b');
    block["timestamp"] = Json::Int64(1760000000000);
    block["sealer"] = 2;
    block["gasUsed"] = "0";
    block["txsRoot"] = "0xt";
    block["receiptsRoot"] = "0xr";
    block["stateRoot"] = "0xs";
    block["parentInfo"] = Json::Value(Json::arrayValue);
    Json::Value parent;
    parent["blockHash"] = "0xp";
    block["parentInfo"].append(parent);
    block["signatureList"] = Json::Value(Json::arrayValue);
    block["signatureList"].append(Json::objectValue);
    block["transactions"] = Json::Value(Json::arrayValue);
    Json::Value tx;
    tx["hash"] = "0x9c";
    tx["from"] = "0xf1";
    tx["to"] = "0xa1";
    block["transactions"].append(tx);
    return block;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(BlockCmdTest)

BOOST_AUTO_TEST_CASE(heightUsesGetBlockByNumberHeaderOnly)
{
    FakeRpc rpc;
    rpc.set("getBlockByNumber", sampleBlock());
    std::ostringstream out;
    auto code = runBlockOn(blockConnection(rpc), blockArgs({"get", "5", "--json"}), out);
    BOOST_CHECK_EQUAL(code, c_exitOk);
    auto const& [method, params] = rpc.calls().back();
    BOOST_CHECK_EQUAL(method, "getBlockByNumber");
    BOOST_CHECK_EQUAL(params[0].asString(), "group0");
    BOOST_CHECK_EQUAL(params[2].asInt64(), 5);
    BOOST_CHECK_EQUAL(params[3].asBool(), true);  // onlyHeader
    BOOST_CHECK_EQUAL(params[4].asBool(), true);  // onlyTxHash
    BOOST_CHECK_EQUAL(parseJson(out.str(), "block")["number"].asInt(), 5);
}

BOOST_AUTO_TEST_CASE(hashUsesGetBlockByHashAndTxsFlipsBothFlags)
{
    FakeRpc rpc;
    rpc.set("getBlockByHash", sampleBlock());
    std::ostringstream out;
    auto hash = "0x" + std::string(64, 'b');
    auto code = runBlockOn(blockConnection(rpc), blockArgs({"get", hash, "--txs", "--json"}), out);
    BOOST_CHECK_EQUAL(code, c_exitOk);
    auto const& [method, params] = rpc.calls().back();
    BOOST_CHECK_EQUAL(method, "getBlockByHash");
    BOOST_CHECK_EQUAL(params[2].asString(), hash);
    BOOST_CHECK_EQUAL(params[3].asBool(), false);
    BOOST_CHECK_EQUAL(params[4].asBool(), false);
    auto json = parseJson(out.str(), "block");
    BOOST_CHECK_EQUAL(json["transactions"][0]["from"].asString(), "0xf1");
    BOOST_CHECK(!json.isMember("proofs"));
}

BOOST_AUTO_TEST_CASE(proofFetchesEachTransactionWithProof)
{
    FakeRpc rpc;
    rpc.set("getBlockByNumber", sampleBlock());
    Json::Value tx;
    tx["hash"] = "0x9c";
    tx["txProof"] = Json::Value(Json::arrayValue);
    tx["txProof"].append("0x01");
    rpc.set("getTransaction", tx);
    auto receipt = blockReceipt();
    receipt["txReceiptProof"] = Json::Value(Json::arrayValue);
    receipt["txReceiptProof"].append("0x02");
    receipt["txReceiptProof"].append("0x03");
    rpc.set("getTransactionReceipt", receipt);
    std::ostringstream out;
    auto code = runBlockOn(blockConnection(rpc), blockArgs({"get", "5", "--proof", "--json"}), out);
    BOOST_CHECK_EQUAL(code, c_exitOk);
    auto json = parseJson(out.str(), "block");
    BOOST_REQUIRE_EQUAL(json["proofs"].size(), 1U);
    BOOST_CHECK_EQUAL(json["proofs"][0]["hash"].asString(), "0x9c");
    BOOST_CHECK_EQUAL(json["proofs"][0]["txProof"].size(), 1U);
    BOOST_CHECK_EQUAL(json["proofs"][0]["receiptProof"].size(), 2U);
    // the block itself was fetched with hashes only, the proofs with requireProof=true
    BOOST_CHECK_EQUAL(rpc.calls()[0].first, "getBlockByNumber");
    BOOST_CHECK_EQUAL(rpc.calls()[0].second[3].asBool(), false);
    BOOST_CHECK_EQUAL(rpc.calls()[0].second[4].asBool(), true);
    BOOST_CHECK_EQUAL(rpc.calls()[1].first, "getTransaction");
    BOOST_CHECK_EQUAL(rpc.calls()[1].second[3].asBool(), true);
}

BOOST_AUTO_TEST_CASE(badReferenceIsUsage)
{
    FakeRpc rpc;
    std::ostringstream out;
    BOOST_CHECK_THROW(runBlockOn(blockConnection(rpc), blockArgs({"get", "0x12"}), out), OpsError);
    BOOST_CHECK_THROW(runBlockOn(blockConnection(rpc), blockArgs({"get", "-1"}), out), OpsError);
    BOOST_CHECK_THROW(runBlockOn(blockConnection(rpc), blockArgs({"get", "five"}), out), OpsError);
    BOOST_CHECK_THROW(runBlockOn(blockConnection(rpc), blockArgs({"list"}), out), OpsError);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
