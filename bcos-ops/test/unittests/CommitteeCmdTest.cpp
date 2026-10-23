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
 * @file CommitteeCmdTest.cpp
 */
#include "bcos-ops/cmd/CommitteeCmd.h"
#include "bcos-ops/OpsError.h"
#include "bcos-ops/tx/Abi.h"
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
std::string const c_cmtManager = "0x0000000000000000000000000000000000010001";
std::string const c_cmtCommittee = "0xc0ffee0000000000000000000000000000000001";
std::string const c_cmtProposalMgr = "0x5c2d3b4a1f0e9d8c7b6a59483726150493827161";
std::string const c_cmtGovernor = "0x3f12000000000000000000000000000000009a00";

std::string cmtOutput(std::string const& _types, std::string const& _jsonValues)
{
    auto data = Abi(false).encodeMethodBySignature("o(" + _types + ")", _jsonValues);
    return "0x" + toHex(bytes(data.begin() + 4, data.end()));
}

std::string cmtSelector(std::string const& _signature)
{
    return bcos::crypto::Keccak256().hash(bytesConstRef(_signature)).hex().substr(0, 8);
}

Json::Value cmtCallResult(std::string const& _output)
{
    Json::Value value;
    value["status"] = 0;
    value["output"] = _output;
    return value;
}

Json::Value cmtReceipt(std::string const& _output)
{
    Json::Value receipt;
    receipt["status"] = 0;
    receipt["transactionHash"] = "0x9c";
    receipt["blockNumber"] = 129;
    receipt["gasUsed"] = "21000";
    receipt["output"] = _output;
    receipt["logEntries"] = Json::Value(Json::arrayValue);
    return receipt;
}

FakeRpc cmtRpc()
{
    FakeRpc rpc;
    rpc.setCall(cmtSelector("_committee()"),
        cmtCallResult(cmtOutput("address", "[\"" + c_cmtCommittee + "\"]")));
    rpc.setCall(cmtSelector("_proposalMgr()"),
        cmtCallResult(cmtOutput("address", "[\"" + c_cmtProposalMgr + "\"]")));
    rpc.setCall(cmtSelector("getCommitteeInfo()"),
        cmtCallResult(cmtOutput("uint8,uint8,address[],uint32[]",
            "[0,0,[\"" + c_cmtGovernor + "\",\"" + c_cmtCommittee + "\"],[1,2]]")));
    rpc.setCall(cmtSelector("_proposalCount()"), cmtCallResult(cmtOutput("uint256", "[3]")));
    rpc.setCall(
        cmtSelector("getProposalStatus(uint256)"), cmtCallResult(cmtOutput("uint8", "[1]")));
    rpc.setCall(cmtSelector("getProposalInfo(uint256)"),
        cmtCallResult(cmtOutput("address,address,uint8,uint256,uint8,address[],address[]",
            "[\"" + c_cmtManager + "\",\"" + c_cmtGovernor + "\",11,1128,1,[\"" + c_cmtGovernor +
                "\"],[]]")));
    return rpc;
}

Connection cmtConnection(FakeRpc& _rpc)
{
    Connection connection;
    connection.call = _rpc.call();
    connection.source = "attach";
    connection.group = "group0";
    return connection;
}

Args cmtArgs(std::vector<std::string> _argv)
{
    return Args(std::move(_argv), {"json", "yes", "direct"});
}

bcostars::TransactionUniquePtr cmtSentTx(FakeRpc const& _rpc)
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

BOOST_AUTO_TEST_SUITE(CommitteeCmdTest)

BOOST_AUTO_TEST_CASE(infoResolvesTheCommitteeAndListsGovernors)
{
    auto rpc = cmtRpc();
    std::ostringstream out;
    BOOST_CHECK_EQUAL(
        runCommitteeOn(cmtConnection(rpc), cmtArgs({"info", "--json"}), out), c_exitOk);
    auto json = parseJson(out.str(), "info");
    BOOST_CHECK_EQUAL(json["committee"].asString(), c_cmtCommittee);
    BOOST_CHECK_EQUAL(json["participatesRate"].asString(), "0");
    BOOST_CHECK_EQUAL(json["winRate"].asString(), "0");
    BOOST_REQUIRE_EQUAL(json["governors"].size(), 2U);
    BOOST_CHECK_EQUAL(json["governors"][0]["address"].asString(), c_cmtGovernor);
    BOOST_CHECK_EQUAL(json["governors"][1]["weight"].asString(), "2");
    // getCommitteeInfo went to the resolved committee address, not to the manager
    bool seen = false;
    for (auto const& [method, params] : rpc.calls())
    {
        if (method == "call" && params[3].asString().starts_with(cmtSelector("getCommitteeInfo()")))
        {
            BOOST_CHECK_EQUAL(params[2].asString(), c_cmtCommittee);
            seen = true;
        }
    }
    BOOST_CHECK(seen);
}

BOOST_AUTO_TEST_CASE(proposalsListsTheLastN)
{
    auto rpc = cmtRpc();
    std::ostringstream out;
    BOOST_CHECK_EQUAL(
        runCommitteeOn(cmtConnection(rpc), cmtArgs({"proposals", "--last", "2", "--json"}), out),
        c_exitOk);
    auto json = parseJson(out.str(), "proposals");
    BOOST_CHECK_EQUAL(json["count"].asInt(), 3);
    BOOST_CHECK_EQUAL(json["proposalManager"].asString(), c_cmtProposalMgr);
    BOOST_REQUIRE_EQUAL(json["proposals"].size(), 2U);
    BOOST_CHECK_EQUAL(json["proposals"][0]["id"].asString(), "2");
    BOOST_CHECK_EQUAL(json["proposals"][1]["id"].asString(), "3");
    BOOST_CHECK_EQUAL(json["proposals"][1]["statusName"].asString(), "voting");
    BOOST_CHECK_EQUAL(json["proposals"][1]["proposalType"].asString(), "11");
    BOOST_CHECK_EQUAL(json["proposals"][1]["agreeVoters"].size(), 1U);
    // the CLI names the type with the same mapping the TUI uses
    BOOST_CHECK_EQUAL(json["proposals"][1]["proposalTypeName"].asString(), "governor");
    std::ostringstream one;
    runCommitteeOn(cmtConnection(rpc), cmtArgs({"proposal", "3", "--json"}), one);
    auto detail = parseJson(one.str(), "proposal");
    BOOST_CHECK_EQUAL(detail["proposer"].asString(), c_cmtGovernor);
    // the detail is the same outcome a write reports: expiry block, executed flag, status name
    BOOST_CHECK_EQUAL(detail["expiresAtBlock"].asString(), "1128");  // uint256 decodes as text
    BOOST_CHECK_EQUAL(detail["statusName"].asString(), "voting");
    BOOST_CHECK_EQUAL(detail["proposalTypeName"].asString(), "governor");
    BOOST_CHECK(!detail["executed"].asBool());
    BOOST_CHECK(!detail.isMember("blockNumberInterval"));
}

BOOST_AUTO_TEST_CASE(voteAndRevokeAreDirectWritesToTheManager)
{
    auto rpc = cmtRpc();
    rpc.queue("sendTransaction", cmtReceipt("0x"));
    std::ostringstream out;
    BOOST_CHECK_EQUAL(
        runCommitteeOn(cmtConnection(rpc), cmtArgs({"vote", "5", "true", "--yes", "--json"}), out),
        c_exitOk);
    auto json = parseJson(out.str(), "vote");
    BOOST_CHECK_EQUAL(json["mode"].asString(), "direct");
    BOOST_CHECK_EQUAL(json["method"].asString(), "voteProposal(uint256,bool)");
    BOOST_CHECK_EQUAL(json["params"][0].asString(), "5");
    BOOST_CHECK(json["params"][1].asBool());
    BOOST_CHECK(json["ok"].asBool());
    // the vote reports the proposal it touched (status read back from ProposalManager)
    BOOST_CHECK_EQUAL(json["proposal"]["id"].asString(), "5");
    BOOST_CHECK_EQUAL(json["proposal"]["statusName"].asString(), "voting");
    auto tx = cmtSentTx(rpc);
    BOOST_CHECK_EQUAL(tx->data.to, c_cmtManager);
    BOOST_CHECK_EQUAL(
        toHex(tx->data.input).substr(0, 8), cmtSelector("voteProposal(uint256,bool)"));
    std::ostringstream err;
    BOOST_CHECK_THROW(
        runCommitteeOn(cmtConnection(rpc), cmtArgs({"revoke", "--yes"}), err), OpsError);
}

BOOST_AUTO_TEST_CASE(proposeGovernorReportsTheProposal)
{
    auto rpc = cmtRpc();
    rpc.queue("sendTransaction", cmtReceipt(cmtOutput("uint256", "[9]")));
    std::ostringstream out;
    auto code = runCommitteeOn(cmtConnection(rpc),
        cmtArgs({"propose-governor", c_cmtGovernor, "1", "--interval", "7", "--yes", "--json"}),
        out);
    BOOST_CHECK_EQUAL(code, c_exitOk);
    auto json = parseJson(out.str(), "propose-governor");
    BOOST_CHECK_EQUAL(json["mode"].asString(), "proposal");
    BOOST_CHECK_EQUAL(
        json["method"].asString(), "createUpdateGovernorProposal(address,uint32,uint256)");
    BOOST_CHECK_EQUAL(json["params"][2].asString(), "7");
    BOOST_CHECK_EQUAL(json["proposal"]["id"].asString(), "9");
    BOOST_CHECK_EQUAL(json["proposal"]["statusName"].asString(), "voting");
    BOOST_CHECK(json["ok"].asBool());
    // without --interval the default is 1000 blocks from now (ProposalManager.sol:102 adds the
    // interval to block.number), fetched from no RPC
    rpc.queue("sendTransaction", cmtReceipt(cmtOutput("uint256", "[10]")));
    std::ostringstream dflt;
    runCommitteeOn(cmtConnection(rpc),
        cmtArgs({"propose-sysconfig", "tx_count_limit", "2000", "--yes", "--json"}), dflt);
    BOOST_CHECK_EQUAL(parseJson(dflt.str(), "default")["params"][2].asString(), "1000");
    // the only height lookups are the blockLimit of each sent transaction, none for the interval
    auto countOf = [&](std::string const& method) {
        return std::ranges::count_if(
            rpc.calls(), [&](auto const& call) { return call.first == method; });
    };
    BOOST_CHECK_EQUAL(countOf("getBlockNumber"), countOf("sendTransaction"));
    std::ostringstream err;
    BOOST_CHECK_THROW(runCommitteeOn(cmtConnection(rpc),
                          cmtArgs({"propose-sysconfig", "tx_count_limit", "2000", "--interval", "0",
                              "--yes", "--json"}),
                          err),
        OpsError);
    BOOST_CHECK_THROW(runCommitteeOn(cmtConnection(rpc),
                          cmtArgs({"propose-sysconfig", "tx_count_limit", "2000", "--interval",
                              "-5", "--yes", "--json"}),
                          err),
        OpsError);
}

BOOST_AUTO_TEST_CASE(usageErrors)
{
    auto rpc = cmtRpc();
    std::ostringstream out;
    BOOST_CHECK_THROW(runCommitteeOn(cmtConnection(rpc), cmtArgs({"bogus"}), out), OpsError);
    BOOST_CHECK_THROW(runCommitteeOn(cmtConnection(rpc), cmtArgs({"proposal"}), out), OpsError);
    BOOST_CHECK_THROW(
        runCommitteeOn(cmtConnection(rpc), cmtArgs({"propose-rate", "50"}), out), OpsError);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
