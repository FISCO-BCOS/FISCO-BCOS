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
 * @brief the send page's worker: a key file loads into Model.keyAddress, a preset request
 *        sends through the table and records the preset for the tx detail, raw @file reads the
 *        file, a proposal outcome carries the id and status, writes without a key fail with a
 *        line, the thread stops on request
 * @file SenderTest.cpp
 */
#include "bcos-ops/tui/Sender.h"
#include "bcos-ops/tui/Model.h"
#include "bcos-ops/tx/Abi.h"
#include "bcos-ops/tx/Presets.h"
#include "fixtures/FakeRpc.h"
#include <bcos-cpp-sdk/utilities/tx/TransactionBuilder.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <unistd.h>
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <thread>

namespace bcos::ops::test
{
namespace
{
std::string const c_senderKeyHex =
    "0x1111111111111111111111111111111111111111111111111111111111111111";
std::string const c_senderCommittee = "0x0000000000000000000000000000000000010001";

Connection senderConnection(FakeRpc& _rpc)
{
    Connection connection;
    connection.call = _rpc.call();
    connection.source = "rpc";
    connection.group = "group0";
    return connection;
}

/// ABI-encoded output of a call: a fake method with the output types, selector dropped
std::string senderOutput(std::string const& _types, std::string const& _jsonValues)
{
    auto data = Abi(false).encodeMethodBySignature("o(" + _types + ")", _jsonValues);
    return "0x" + toHex(bytes(data.begin() + 4, data.end()));
}

std::string senderSelector(std::string const& _signature)
{
    return bcos::crypto::Keccak256().hash(bytesConstRef(_signature)).hex().substr(0, 8);
}

Json::Value senderReceipt(std::string const& _output, std::string const& _contract = "")
{
    Json::Value receipt;
    receipt["status"] = 0;
    receipt["transactionHash"] = "0x9c";
    receipt["blockNumber"] = 129;
    receipt["gasUsed"] = "21000";
    receipt["contractAddress"] = _contract;
    receipt["output"] = _output;
    receipt["logEntries"] = Json::Value(Json::arrayValue);
    return receipt;
}

std::filesystem::path senderTempDir()
{
    // per process: ctest runs the cases in parallel and several write key.hex here
    auto dir =
        std::filesystem::temp_directory_path() / ("ops-sender-test-" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir);
    return dir;
}

tui::SendRequest senderLoadKey()
{
    auto path = (senderTempDir() / "key.hex").string();
    std::ofstream(path) << c_senderKeyHex << '\n';
    tui::SendRequest request;
    request.kind = tui::SendRequest::Kind::loadKey;
    request.keyFile = path;
    return request;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(SenderTest)

BOOST_AUTO_TEST_CASE(loadKeyPublishesTheAddressAndTheRouting)
{
    FakeRpc rpc;
    tui::Model model;
    int updates = 0;
    tui::Sender sender(senderConnection(rpc), model, [&]() { ++updates; });
    BOOST_CHECK(!sender.hasKey());
    sender.execute(senderLoadKey());
    BOOST_CHECK(sender.hasKey());
    BOOST_CHECK_EQUAL(model.keyAddress.size(), 42U);
    BOOST_CHECK_EQUAL(model.send.last.seq, 1U);
    BOOST_CHECK(model.send.last.ok);
    BOOST_CHECK(model.send.last.line.starts_with("key loaded: 0x"));
    BOOST_CHECK(model.send.routingKnown);
    BOOST_CHECK(!model.send.proposals);  // auth_check_status unset, no node dir
    BOOST_CHECK_EQUAL(updates, 1);
    BOOST_CHECK(!model.send.busy);
    // a bad file is a line, not an exception
    tui::SendRequest bad;
    bad.kind = tui::SendRequest::Kind::loadKey;
    bad.keyFile = (senderTempDir() / "missing.pem").string();
    sender.execute(bad);
    BOOST_CHECK(!model.send.last.ok);
    BOOST_CHECK(model.send.last.line.starts_with("error:"));
    BOOST_CHECK_EQUAL(model.send.last.seq, 2U);
}

BOOST_AUTO_TEST_CASE(presetWriteSendsAndRecordsThePresetForTheDetail)
{
    FakeRpc rpc;
    rpc.set("sendTransaction", senderReceipt(senderOutput("int32", "[0]")));
    tui::Model model;
    tui::Sender sender(senderConnection(rpc), model, nullptr);
    auto const* addObserver = findPreset("consensus", "add-observer");
    BOOST_REQUIRE(addObserver != nullptr);
    tui::SendRequest request;
    request.kind = tui::SendRequest::Kind::preset;
    request.preset = addObserver;
    request.params = {"3a1f00"};
    sender.execute(request);  // no key yet
    BOOST_CHECK(!model.send.last.ok);
    BOOST_CHECK_EQUAL(model.send.last.line, "error: load a key first");
    sender.execute(senderLoadKey());
    sender.execute(request);
    BOOST_CHECK(model.send.last.ok);
    BOOST_CHECK_EQUAL(model.send.last.txHash, "0x9c");
    BOOST_CHECK(model.send.last.line.find("block 129") != std::string::npos);
    BOOST_CHECK(model.send.last.line.find("ok") != std::string::npos);
    BOOST_REQUIRE(model.sentPresets.contains("0x9c"));
    BOOST_CHECK_EQUAL(model.sentPresets["0x9c"], addObserver);
    // a precompiled code is the line and not ok
    rpc.set("sendTransaction", senderReceipt(senderOutput("int32", "[-51104]")));
    sender.execute(request);
    BOOST_CHECK(!model.send.last.ok);
    BOOST_CHECK(model.send.last.line.find("SEALER_MUST_BE_OBSERVER_FIRST") != std::string::npos);
    // a read preset needs no key and shows its decoded value
    FakeRpc reader;
    tui::Model readModel;
    tui::Sender readSender(senderConnection(reader), readModel, nullptr);
    auto const* get = findPreset("hello", "get");
    BOOST_REQUIRE(get != nullptr);
    Json::Value callResult;
    callResult["status"] = 0;
    callResult["output"] = senderOutput("string", R"(["hi there"])");
    reader.set("call", callResult);
    tui::SendRequest read;
    read.kind = tui::SendRequest::Kind::preset;
    read.preset = get;
    read.to = "0xc02d";
    readSender.execute(read);
    BOOST_CHECK(readModel.send.last.ok);
    BOOST_CHECK(readModel.send.last.line.find("hi there") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(helloDeployRawFileAndCustomCall)
{
    FakeRpc rpc;
    rpc.set("sendTransaction", senderReceipt("0x", "0xc02d0000000000000000000000000000000000aa"));
    tui::Model model;
    tui::Sender sender(senderConnection(rpc), model, nullptr);
    sender.execute(senderLoadKey());
    tui::SendRequest deploy;
    deploy.kind = tui::SendRequest::Kind::helloDeploy;
    sender.execute(deploy);
    BOOST_CHECK(model.send.last.ok);
    BOOST_CHECK_EQUAL(model.send.helloAddress, "0xc02d0000000000000000000000000000000000aa");
    BOOST_CHECK(model.send.last.line.find("HelloWorld deployed at 0xc02d") != std::string::npos);
    // raw @file: the file's hex goes out verbatim (0x stripped)
    auto path = (senderTempDir() / "signed.hex").string();
    std::ofstream(path) << "0xdeadbeef\n";
    tui::SendRequest raw;
    raw.kind = tui::SendRequest::Kind::raw;
    raw.raw = "@" + path;
    sender.execute(raw);
    BOOST_CHECK(model.send.last.ok);
    BOOST_CHECK(model.send.last.line.starts_with("raw tx"));
    auto const& last = rpc.calls().back();
    BOOST_CHECK_EQUAL(last.first, "sendTransaction");
    BOOST_CHECK_EQUAL(last.second[2].asString(), "deadbeef");
    // custom: signature + params encoded, value passes through
    tui::SendRequest custom;
    custom.kind = tui::SendRequest::Kind::custom;
    custom.to = "0xc02d0000000000000000000000000000000000aa";
    custom.signature = "set(string)";
    custom.params = {"hi"};
    sender.execute(custom);
    BOOST_CHECK(model.send.last.ok);
    BOOST_CHECK(model.send.last.line.find("block 129 ok") != std::string::npos);
    BOOST_CHECK_EQUAL(rpc.calls().back().first, "sendTransaction");
}

BOOST_AUTO_TEST_CASE(proposalOutcomeCarriesIdAndStatus)
{
    FakeRpc rpc;
    Json::Value authOn;
    authOn["value"] = "1";
    authOn["blockNumber"] = 2;
    rpc.set("getSystemConfigByKey", authOn);
    auto receipt = senderReceipt(senderOutput("uint256", "[7]"));
    Json::Value log;
    log["address"] = c_senderCommittee;
    log["topics"] = Json::Value(Json::arrayValue);
    log["topics"].append(
        "0x" + bcos::crypto::Keccak256().hash(bytesConstRef("execResult(int256)")).hex());
    log["data"] = senderOutput("int256", "[0]");
    receipt["logEntries"].append(log);
    rpc.set("sendTransaction", receipt);
    Json::Value manager;
    manager["status"] = 0;
    manager["output"] =
        senderOutput("address", R"(["0x5c2d3b4a1f0e9d8c7b6a59483726150493827161"])");
    rpc.setCall(senderSelector("_proposalMgr()"), manager);
    Json::Value status;
    status["status"] = 0;
    status["output"] = senderOutput("uint8", "[2]");
    rpc.setCall(senderSelector("getProposalStatus(uint256)"), status);
    Json::Value info;
    info["status"] = 0;
    info["output"] = senderOutput("address,address,uint8,uint256,uint8,address[],address[]",
        R"(["0x0000000000000000000000000000000000001003","0x3f12000000000000000000000000000000009a00",51,1128,2,["0x3f12000000000000000000000000000000009a00"],[]])");
    rpc.setCall(senderSelector("getProposalInfo(uint256)"), info);
    tui::Model model;
    tui::Sender sender(senderConnection(rpc), model, nullptr);
    sender.execute(senderLoadKey());
    BOOST_CHECK(model.send.proposals);
    tui::SendRequest request;
    request.kind = tui::SendRequest::Kind::preset;
    request.preset = findPreset("consensus", "add-observer");
    request.params = {"3a1f00"};
    request.routing.proposals = model.send.proposals;  // what the confirmation layer showed
    sender.execute(request);
    BOOST_CHECK(model.send.last.ok);
    BOOST_CHECK_EQUAL(model.send.last.proposalId, "7");
    BOOST_CHECK_EQUAL(model.send.last.proposalStatus, "passed");
    BOOST_CHECK(model.send.last.line.starts_with("proposal 7 passed executed"));
}

BOOST_AUTO_TEST_CASE(workerThreadRunsQueuedRequestsAndStops)
{
    FakeRpc rpc;
    tui::Model model;
    tui::Sender sender(senderConnection(rpc), model, nullptr);
    sender.start();
    tui::SendRequest read;
    read.kind = tui::SendRequest::Kind::readFeatures;
    sender.submit(read);
    {
        std::lock_guard<std::mutex> lock(model.mutex);
        BOOST_CHECK(model.send.busy);
    }
    for (int i = 0; i < 200; ++i)
    {
        {
            std::lock_guard<std::mutex> lock(model.mutex);
            if (model.send.last.seq == 1)
            {
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    sender.stop();
    BOOST_CHECK_EQUAL(model.send.last.seq, 1U);
    BOOST_CHECK(model.send.featuresLoaded);
    BOOST_CHECK(!model.send.features.empty());
    BOOST_CHECK(!model.send.busy);
    sender.stop();  // idempotent
}

BOOST_AUTO_TEST_CASE(theRequestsRoutingIsSentVerbatim)
{
    // the chain says auth, the request says direct (what the layer showed): direct is sent
    FakeRpc rpc;
    Json::Value authOn;
    authOn["value"] = "1";
    authOn["blockNumber"] = 2;
    rpc.set("getSystemConfigByKey", authOn);
    rpc.set("sendTransaction", senderReceipt(senderOutput("int32", "[0]")));
    tui::Model model;
    tui::Sender sender(senderConnection(rpc), model, nullptr);
    sender.execute(senderLoadKey());
    BOOST_CHECK(model.send.proposals);
    tui::SendRequest request;
    request.kind = tui::SendRequest::Kind::preset;
    request.preset = findPreset("consensus", "add-observer");
    request.params = {"3a1f00"};
    request.routing.proposals = false;
    sender.execute(request);
    BOOST_CHECK(model.send.last.ok);
    BOOST_CHECK(model.send.last.proposalId.empty());
    bool sentDirect = false;
    for (auto const& [method, params] : rpc.calls())
    {
        if (method == "sendTransaction")
        {
            bcos::cppsdk::utilities::TransactionBuilder builder;
            auto tx = builder.decodeTransaction(fromHex(params[2].asString()));
            sentDirect = tx->data.to == "0x0000000000000000000000000000000000001003";
        }
    }
    BOOST_CHECK(sentDirect);
    // no second routing question was asked on the way
    size_t configReads = 0;
    for (auto const& [method, params] : rpc.calls())
    {
        configReads += method == "getSystemConfigByKey" ? 1 : 0;
    }
    BOOST_CHECK_EQUAL(configReads, 1U);  // the key load only
}

BOOST_AUTO_TEST_CASE(stopInsideAnEnableFeaturesRequestEndsIt)
{
    FakeRpc rpc;
    tui::Model model;
    // the fake counts the calls that reach it and asks the sender to stop on the second
    size_t reached = 0;
    bool armed = false;  // only the enableFeatures request trips the stop
    std::function<void()> stopHook;
    Connection connection = senderConnection(rpc);
    auto inner = connection.call;
    connection.call = [&](std::string_view _method, Json::Value const& _params) {
        ++reached;
        if (armed && reached == 2)
        {
            stopHook();
        }
        return inner(_method, _params);
    };
    tui::Sender sender(connection, model, nullptr);
    stopHook = [&]() { sender.stop(); };
    sender.execute(senderLoadKey());  // reads the chain facts and the routing first
    reached = 0;
    armed = true;
    tui::SendRequest request;
    request.kind = tui::SendRequest::Kind::enableFeatures;
    request.features = {"feature_balance", "feature_balance_precompiled"};
    sender.execute(request);
    BOOST_CHECK_EQUAL(reached, 2U);  // the call in flight finished; no third RPC started
    BOOST_CHECK(!model.send.last.ok);
    BOOST_CHECK(model.send.last.line.find("send stopped") != std::string::npos);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
