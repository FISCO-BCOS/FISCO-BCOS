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
 *  limitations under the License. *
 * @brief on an auth chain (auth_check_status = 1) the consensus / sysconfig writes become
 *        CommitteeManager proposals; --direct keeps the precompiled call; the result carries the
 *        proposal id, status and the execResult code
 * @file ProposalRoutingTest.cpp
 */
#include "bcos-ops/OpsError.h"
#include "bcos-ops/cmd/SysconfigCmd.h"
#include "bcos-ops/tx/Abi.h"
#include "bcos-ops/tx/Account.h"
#include "bcos-ops/tx/PresetCall.h"
#include "fixtures/FakeRpc.h"
#include <bcos-cpp-sdk/utilities/tx/TransactionBuilder.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <sstream>

namespace bcos::ops::test
{
namespace
{
std::string const c_routeCommittee = "0x0000000000000000000000000000000000010001";
std::string const c_routeManager = "0x5c2d3b4a1f0e9d8c7b6a59483726150493827161";
std::string const c_routeNode = "74637d36";

/// ABI-encoded output of a call: encode a fake method with the output types, drop the selector
std::string routeOutput(std::string const& _types, std::string const& _jsonValues)
{
    auto data = Abi(false).encodeMethodBySignature("o(" + _types + ")", _jsonValues);
    return "0x" + toHex(bytes(data.begin() + 4, data.end()));
}

std::string routeSelector(std::string const& _signature)
{
    return bcos::crypto::Keccak256().hash(bytesConstRef(_signature)).hex().substr(0, 8);
}

Json::Value routeCallResult(std::string const& _output)
{
    Json::Value value;
    value["status"] = 0;
    value["output"] = _output;
    return value;
}

/// receipt of a create*Proposal tx: output = proposal id, execResult(int256) log with _execCode
Json::Value routeCreateReceipt(int _id, std::optional<int32_t> _execCode)
{
    Json::Value receipt;
    receipt["status"] = 0;
    receipt["transactionHash"] = "0x9c";
    receipt["blockNumber"] = 129;
    receipt["gasUsed"] = "21000";
    receipt["output"] = routeOutput("uint256", "[" + std::to_string(_id) + "]");
    receipt["logEntries"] = Json::Value(Json::arrayValue);
    if (_execCode)
    {
        Json::Value log;
        log["address"] = c_routeCommittee;
        log["topics"] = Json::Value(Json::arrayValue);
        log["topics"].append(
            "0x" + bcos::crypto::Keccak256().hash(bytesConstRef("execResult(int256)")).hex());
        log["data"] = routeOutput("int256", "[" + std::to_string(*_execCode) + "]");
        receipt["logEntries"].append(log);
    }
    return receipt;
}

FakeRpc routeRpc(int _status, std::optional<int32_t> _execCode = 0)
{
    FakeRpc rpc;
    Json::Value authOn;
    authOn["value"] = "1";
    authOn["blockNumber"] = 2;
    rpc.set("getSystemConfigByKey", authOn);
    rpc.queue("sendTransaction", routeCreateReceipt(7, _execCode));
    rpc.setCall(routeSelector("_proposalMgr()"),
        routeCallResult(routeOutput("address", "[\"" + c_routeManager + "\"]")));
    rpc.setCall(routeSelector("getProposalStatus(uint256)"),
        routeCallResult(routeOutput("uint8", "[" + std::to_string(_status) + "]")));
    rpc.setCall(routeSelector("getProposalInfo(uint256)"),
        routeCallResult(routeOutput("address,address,uint8,uint256,uint8,address[],address[]",
            R"(["0x0000000000000000000000000000000000001003","0x3f12000000000000000000000000000000009a00",51,1128,)" +
                std::to_string(_status) +
                R"(,["0x3f12000000000000000000000000000000009a00"],[]])")));
    return rpc;
}

Connection routeConnection(FakeRpc& _rpc)
{
    Connection connection;
    connection.call = _rpc.call();
    connection.source = "attach";
    connection.group = "group0";
    return connection;
}

Args routeArgs(std::vector<std::string> _argv)
{
    return Args(std::move(_argv), {"json", "yes", "direct"});
}

bcostars::TransactionUniquePtr routeSentTx(FakeRpc const& _rpc)
{
    for (auto const& [method, params] : _rpc.calls())
    {
        if (method == "sendTransaction")
        {
            bcos::cppsdk::utilities::TransactionBuilder builder;
            return builder.decodeTransaction(fromHex(params[2].asString()));
        }
    }
    BOOST_FAIL("no sendTransaction call");
    return nullptr;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(ProposalRoutingTest)

BOOST_AUTO_TEST_CASE(addObserverBecomesASetConsensusWeightProposal)
{
    auto rpc = routeRpc(2);
    std::ostringstream out;
    auto code = runConsensusOn(
        routeConnection(rpc), routeArgs({"add-observer", c_routeNode, "--yes", "--json"}), out);
    BOOST_CHECK_EQUAL(code, c_exitOk);
    auto json = parseJson(out.str(), "add-observer");
    BOOST_CHECK_EQUAL(json["mode"].asString(), "proposal");
    BOOST_CHECK_EQUAL(json["to"].asString(), c_routeCommittee);
    BOOST_CHECK_EQUAL(
        json["method"].asString(), "createSetConsensusWeightProposal(string,uint32,bool,uint256)");
    BOOST_CHECK_EQUAL(json["directMethod"].asString(), "addObserver(string)");
    BOOST_REQUIRE_EQUAL(json["params"].size(), 4U);
    BOOST_CHECK_EQUAL(json["params"][0].asString(), c_routeNode);
    BOOST_CHECK_EQUAL(json["params"][1].asString(), "0");
    BOOST_CHECK_EQUAL(json["params"][2].asBool(), true);
    BOOST_CHECK_EQUAL(json["params"][3].asString(), "1000");  // relative, no height lookup
    BOOST_CHECK_EQUAL(json["proposal"]["id"].asString(), "7");
    BOOST_CHECK_EQUAL(json["proposal"]["statusName"].asString(), "passed");
    BOOST_CHECK(json["proposal"]["executed"].asBool());
    BOOST_CHECK_EQUAL(json["proposal"]["execCode"].asInt(), 0);
    BOOST_CHECK_EQUAL(json["proposal"]["info"]["agreeVoters"].size(), 1U);
    BOOST_CHECK(json["ok"].asBool());
    auto tx = routeSentTx(rpc);
    BOOST_CHECK_EQUAL(tx->data.to, c_routeCommittee);
    BOOST_CHECK_EQUAL(toHex(tx->data.input).substr(0, 8),
        routeSelector("createSetConsensusWeightProposal(string,uint32,bool,uint256)"));
}

BOOST_AUTO_TEST_CASE(directFlagKeepsThePrecompiledCall)
{
    auto rpc = routeRpc(2);
    rpc.queue("sendTransaction", routeCreateReceipt(0, std::nullopt));  // unused queue entry
    std::ostringstream out;
    runConsensusOn(routeConnection(rpc),
        routeArgs({"add-observer", c_routeNode, "--direct", "--yes", "--json"}), out);
    auto json = parseJson(out.str(), "direct");
    BOOST_CHECK_EQUAL(json["mode"].asString(), "direct");
    BOOST_CHECK_EQUAL(json["to"].asString(), "0x0000000000000000000000000000000000001003");
    BOOST_CHECK(!json.isMember("proposal"));
    // --direct never asks the chain whether it is an auth chain
    for (auto const& [method, params] : rpc.calls())
    {
        BOOST_CHECK_NE(method, "getSystemConfigByKey");
    }
}

BOOST_AUTO_TEST_CASE(sysconfigSetUsesTheIntervalOption)
{
    auto rpc = routeRpc(1);  // two governors: still voting
    std::ostringstream out;
    auto code = runSysconfigOn(routeConnection(rpc),
        routeArgs({"set", "tx_count_limit", "2000", "--interval", "50", "--yes", "--json"}), out);
    BOOST_CHECK_EQUAL(code, c_exitOk);
    auto json = parseJson(out.str(), "set");
    BOOST_CHECK_EQUAL(json["mode"].asString(), "proposal");
    BOOST_CHECK_EQUAL(
        json["method"].asString(), "createSetSysConfigProposal(string,string,uint256)");
    BOOST_CHECK_EQUAL(json["params"][0].asString(), "tx_count_limit");
    BOOST_CHECK_EQUAL(json["params"][1].asString(), "2000");
    BOOST_CHECK_EQUAL(json["params"][2].asString(), "50");
    BOOST_CHECK_EQUAL(json["proposal"]["statusName"].asString(), "voting");
    BOOST_CHECK(!json["proposal"]["executed"].asBool());
    BOOST_CHECK(json["ok"].asBool());  // created and open is a success
}

BOOST_AUTO_TEST_CASE(execResultCodeFailsTheCommand)
{
    auto rpc = routeRpc(2, -51104);  // passed, but the precompiled refused
    std::ostringstream out;
    auto code = runConsensusOn(
        routeConnection(rpc), routeArgs({"add-sealer", c_routeNode, "1", "--yes", "--json"}), out);
    BOOST_CHECK_EQUAL(code, c_exitChecksFailed);
    auto json = parseJson(out.str(), "add-sealer");
    BOOST_CHECK_EQUAL(json["proposal"]["execCode"].asInt(), -51104);
    BOOST_CHECK_EQUAL(json["proposal"]["execCodeName"].asString(), "SEALER_MUST_BE_OBSERVER_FIRST");
    BOOST_CHECK(!json["ok"].asBool());
}

BOOST_AUTO_TEST_CASE(nonAuthChainStaysDirect)
{
    FakeRpc rpc;  // auth_check_status unset, no node dir → direct
    rpc.queue("sendTransaction", routeCreateReceipt(0, std::nullopt));
    std::ostringstream out;
    runConsensusOn(
        routeConnection(rpc), routeArgs({"add-observer", c_routeNode, "--yes", "--json"}), out);
    BOOST_CHECK_EQUAL(parseJson(out.str(), "plain")["mode"].asString(), "direct");
}

BOOST_AUTO_TEST_CASE(proposalArgsGrammar)
{
    FakeRpc rpc;
    auto connection = routeConnection(rpc);
    auto sender = std::make_unique<TxSender>(
        connection.call, "group0", "chain0", false, makeAccount(false, std::nullopt));
    PresetRunner runner(*sender, false, connection.call, "group0");
    auto const* remove = findPreset("consensus", "remove");
    BOOST_CHECK(!runner.routesThroughProposal(*remove));
    runner.setRouting(Routing{.proposals = true});
    BOOST_CHECK(runner.routesThroughProposal(*remove));
    auto args = runner.proposalArgs(*remove, {"abcd"});
    BOOST_REQUIRE_EQUAL(args.size(), 2U);
    BOOST_CHECK_EQUAL(args[0], "abcd");
    BOOST_CHECK_EQUAL(args[1], "1000");  // c_proposalInterval: blocks from now, no RPC
    BOOST_CHECK(std::ranges::none_of(
        rpc.calls(), [](auto const& call) { return call.first == "getBlockNumber"; }));
    // CommitteeManager.sol proposalType 51: addFlag picks addObserver/addSealer, !addFlag setWeight
    auto setWeightArgs = runner.proposalArgs(*findPreset("consensus", "set-weight"), {"ab", "3"});
    BOOST_REQUIRE_EQUAL(setWeightArgs.size(), 4U);
    BOOST_CHECK_EQUAL(setWeightArgs[1], "3");
    BOOST_CHECK_EQUAL(setWeightArgs[2], "false");
    BOOST_CHECK_EQUAL(
        runner.proposalArgs(*findPreset("consensus", "add-sealer"), {"ab", "3"})[2], "true");
    auto observerArgs = runner.proposalArgs(*findPreset("consensus", "add-observer"), {"ab"});
    BOOST_CHECK_EQUAL(observerArgs[1], "0");
    BOOST_CHECK_EQUAL(observerArgs[2], "true");
    runner.setRouting(Routing{.proposals = true, .interval = 0});
    BOOST_CHECK_THROW(runner.proposalArgs(*remove, {"abcd"}), OpsError);
    runner.setRouting(Routing{.proposals = true, .interval = -5});
    BOOST_CHECK_THROW(runner.proposalArgs(*remove, {"abcd"}), OpsError);
    runner.setRouting(Routing{.proposals = true, .interval = 9});
    auto closeArgs = runner.proposalArgs(*findPreset("auth", "close-deploy-auth"), {"0xab"});
    BOOST_REQUIRE_EQUAL(closeArgs.size(), 3U);
    BOOST_CHECK_EQUAL(closeArgs[0], "0xab");
    BOOST_CHECK_EQUAL(closeArgs[1], "false");
    BOOST_CHECK_EQUAL(closeArgs[2], "9");
    BOOST_CHECK(!runner.routesThroughProposal(*findPreset("consensus", "set-term-weight")));
    BOOST_CHECK_EQUAL(PresetRunner::proposalPreset(*remove)->name, "propose-rm-node");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
