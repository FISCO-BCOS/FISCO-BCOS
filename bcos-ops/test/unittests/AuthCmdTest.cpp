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
 * @brief `auth` group over a FakeRpc: read encodings and decoded values (the getMethodAuth
 *        tuple with string[] lists), bytes4 selectors given as 0x…, the four committee writes
 *        becoming proposals on an auth chain (and --direct → -50000), the admin writes staying
 *        direct, and the set-contract-status overload choice
 * @file AuthCmdTest.cpp
 */
#include "bcos-ops/Cli.h"
#include "bcos-ops/OpsError.h"
#include "bcos-ops/cmd/BalanceCmd.h"
#include "bcos-ops/tx/Abi.h"
#include "fixtures/FakeRpc.h"
#include <bcos-cpp-sdk/utilities/tx/TransactionBuilder.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <sstream>

namespace bcos::ops::test
{
namespace
{
std::string const c_authAddress = "0x0000000000000000000000000000000000001005";
std::string const c_authCommittee = "0x0000000000000000000000000000000000010001";
std::string const c_authContract = "0xc02d000000000000000000000000000000000c02";
std::string const c_authAccount = "0x3f12000000000000000000000000000000009a00";
std::string const c_authSelector = "0x4ed3885e";  // set(string)

Connection authConnection(FakeRpc& _rpc)
{
    Connection connection;
    connection.call = _rpc.call();
    connection.source = "attach";
    connection.group = "group0";
    return connection;
}

Args authArgs(std::vector<std::string> _argv)
{
    return Args(std::move(_argv), {"json", "yes", "direct"});
}

std::string authSel(std::string const& _signature)
{
    return bcos::crypto::Keccak256().hash(bytesConstRef(_signature)).hex().substr(0, 8);
}

std::string authOutput(std::string const& _types, std::string const& _jsonValues)
{
    auto data = Abi(false).encodeMethodBySignature("o(" + _types + ")", _jsonValues);
    return "0x" + toHex(bytes(data.begin() + 4, data.end()));
}

Json::Value authCallResult(std::string const& _output)
{
    Json::Value value;
    value["status"] = 0;
    value["output"] = _output;
    return value;
}

Json::Value authReceipt(std::string const& _output)
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

std::string authCallData(FakeRpc const& _rpc)
{
    for (auto const& [method, params] : _rpc.calls())
    {
        if (method == "call")
        {
            return params[3].asString();
        }
    }
    return "";
}

struct AuthSentTx
{
    std::string to;
    std::string input;
};

AuthSentTx authSentTx(FakeRpc const& _rpc)
{
    for (auto const& [method, params] : _rpc.calls())
    {
        if (method == "sendTransaction")
        {
            bcos::cppsdk::utilities::TransactionBuilder builder;
            auto tx = builder.decodeTransaction(fromHex(params[2].asString()));
            return {tx->data.to, toHex(tx->data.input)};
        }
    }
    BOOST_FAIL("no sendTransaction call");
    return {};
}

/// an auth chain (auth_check_status = 1) whose proposals pass at once
FakeRpc authChainRpc()
{
    FakeRpc rpc;
    Json::Value authOn;
    authOn["value"] = "1";
    authOn["blockNumber"] = 2;
    rpc.set("getSystemConfigByKey", authOn);
    rpc.set("sendTransaction", authReceipt(authOutput("uint256", "[9]")));
    rpc.setCall(authSel("_proposalMgr()"),
        authCallResult(authOutput("address", "[\"" + c_authAccount + "\"]")));
    rpc.setCall(authSel("getProposalStatus(uint256)"), authCallResult(authOutput("uint8", "[2]")));
    rpc.setCall(authSel("getProposalInfo(uint256)"),
        authCallResult(authOutput("address,address,uint8,uint256,uint8,address[],address[]",
            "[\"" + c_authAddress + "\",\"" + c_authAccount + "\",21,1128,2,[\"" + c_authAccount +
                "\"],[]]")));
    return rpc;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(AuthCmdTest)

BOOST_AUTO_TEST_CASE(readsEncodeAndDecodeSingleValues)
{
    struct Case
    {
        std::vector<std::string> argv;
        std::string signature;
        std::string outputTypes;
        std::string outputJson;
        std::string expected;
    };
    for (auto const& c : std::vector<Case>{
             {{"get-admin", c_authContract}, "getAdmin(address)", "address",
                 "[\"" + c_authAccount + "\"]", c_authAccount},
             {{"deploy-type"}, "deployType()", "uint256", "[1]", "1"},
             {{"has-deploy-auth", c_authAccount}, "hasDeployAuth(address)", "bool", "[true]",
                 "true"},
             {{"check-method-auth", c_authContract, c_authSelector, c_authAccount},
                 "checkMethodAuth(address,bytes4,address)", "bool", "[false]", "false"},
             {{"contract-available", c_authContract}, "contractAvailable(address)", "bool",
                 "[true]", "true"},
         })
    {
        FakeRpc rpc;
        rpc.setCall(authSel(c.signature), authCallResult(authOutput(c.outputTypes, c.outputJson)));
        auto argv = c.argv;
        argv.push_back("--json");
        std::ostringstream out;
        BOOST_CHECK_EQUAL(runAuthOn(authConnection(rpc), authArgs(argv), out), c_exitOk);
        auto json = parseJson(out.str(), c.signature);
        BOOST_CHECK_EQUAL(json["method"].asString(), c.signature);
        BOOST_CHECK_EQUAL(json["to"].asString(), c_authAddress);
        auto const& value = json["value"];
        BOOST_CHECK_EQUAL(value.isBool()   ? (value.asBool() ? "true" : "false") :
                          value.isString() ? value.asString() :
                                             std::to_string(value.asInt()),
            c.expected);
        BOOST_CHECK(json["ok"].asBool());
        auto data = authCallData(rpc);
        BOOST_CHECK(data.starts_with(authSel(c.signature)));
        if (c.argv[0] == "check-method-auth")
        {
            // bytes4 0x4ed3885e → hex://4ed3885e → left-aligned 32-byte word
            BOOST_CHECK(
                data.find("4ed3885e00000000000000000000000000000000000000000000000000000000") !=
                std::string::npos);
        }
    }
}

BOOST_AUTO_TEST_CASE(getMethodAuthDecodesTheStringLists)
{
    FakeRpc rpc;
    rpc.setCall(authSel("getMethodAuth(address,bytes4)"),
        authCallResult(authOutput(
            "uint8,string[],string[]", "[1,[\"" + c_authAccount + "\"],[\"0xaa\",\"0xbb\"]]")));
    std::ostringstream out;
    BOOST_CHECK_EQUAL(
        runAuthOn(authConnection(rpc),
            authArgs({"get-method-auth", c_authContract, c_authSelector, "--json"}), out),
        c_exitOk);
    auto json = parseJson(out.str(), "get-method-auth");
    // the SDK decodes small integers as decimal strings
    auto const& type = json["value"]["type"];
    BOOST_CHECK_EQUAL(type.isString() ? type.asString() : std::to_string(type.asInt()), "1");
    BOOST_REQUIRE_EQUAL(json["value"]["whitelist"].size(), 1U);
    BOOST_CHECK_EQUAL(json["value"]["whitelist"][0].asString(), c_authAccount);
    BOOST_REQUIRE_EQUAL(json["value"]["blacklist"].size(), 2U);
    BOOST_CHECK_EQUAL(json["value"]["blacklist"][1].asString(), "0xbb");
    std::ostringstream table;
    runAuthOn(
        authConnection(rpc), authArgs({"get-method-auth", c_authContract, c_authSelector}), table);
    BOOST_CHECK(table.str().find("whitelist") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(committeeWritesBecomeProposalsOnAnAuthChain)
{
    struct Case
    {
        std::vector<std::string> argv;
        std::string direct;
        std::string proposal;
    };
    for (auto const& c : std::vector<Case>{
             {{"set-deploy-auth-type", "1"}, "setDeployAuthType(uint8)",
                 "createSetDeployAuthTypeProposal(uint8,uint256)"},
             {{"open-deploy-auth", c_authAccount}, "openDeployAuth(address)",
                 "createModifyDeployAuthProposal(address,bool,uint256)"},
             {{"close-deploy-auth", c_authAccount}, "closeDeployAuth(address)",
                 "createModifyDeployAuthProposal(address,bool,uint256)"},
             {{"reset-admin", c_authContract, c_authAccount}, "resetAdmin(address,address)",
                 "createResetAdminProposal(address,address,uint256)"},
         })
    {
        auto rpc = authChainRpc();
        auto argv = c.argv;
        argv.insert(argv.end(), {"--yes", "--json"});
        std::ostringstream out;
        BOOST_CHECK_EQUAL(runAuthOn(authConnection(rpc), authArgs(argv), out), c_exitOk);
        auto json = parseJson(out.str(), c.proposal);
        BOOST_CHECK_EQUAL(json["mode"].asString(), "proposal");
        BOOST_CHECK_EQUAL(json["to"].asString(), c_authCommittee);
        BOOST_CHECK_EQUAL(json["method"].asString(), c.proposal);
        BOOST_CHECK_EQUAL(json["directMethod"].asString(), c.direct);
        BOOST_CHECK_EQUAL(json["proposal"]["id"].asString(), "9");
        BOOST_CHECK_EQUAL(json["proposal"]["statusName"].asString(), "passed");
        BOOST_CHECK_EQUAL(json["params"][json["params"].size() - 1].asInt(), 1000);  // relative
        if (c.argv[0] == "open-deploy-auth")
        {
            BOOST_CHECK_EQUAL(json["params"][1].asBool(), true);
        }
        if (c.argv[0] == "close-deploy-auth")
        {
            BOOST_CHECK_EQUAL(json["params"][1].asBool(), false);
        }
        if (c.argv[0] == "reset-admin")
        {
            // CommitteeManager.sol:228 createResetAdminProposal(newAdmin, contractAddr, …):
            // the CLI keeps <contract> <admin>, the proposal swaps them
            BOOST_CHECK_EQUAL(json["params"][0].asString(), c_authAccount);
            BOOST_CHECK_EQUAL(json["params"][1].asString(), c_authContract);
        }
        auto tx = authSentTx(rpc);
        BOOST_CHECK_EQUAL(tx.to, c_authCommittee);
        BOOST_CHECK_EQUAL(tx.input.substr(0, 8), authSel(c.proposal));
    }
}

BOOST_AUTO_TEST_CASE(directFlagCallsThePrecompiledAndGetsNoAuthorized)
{
    auto rpc = authChainRpc();
    rpc.set("sendTransaction", authReceipt(authOutput("int32", "[-50000]")));
    std::ostringstream out;
    BOOST_CHECK_EQUAL(
        runAuthOn(authConnection(rpc),
            authArgs({"set-deploy-auth-type", "1", "--direct", "--yes", "--json"}), out),
        c_exitChecksFailed);
    auto json = parseJson(out.str(), "direct");
    BOOST_CHECK_EQUAL(json["mode"].asString(), "direct");
    BOOST_CHECK_EQUAL(json["to"].asString(), c_authAddress);
    BOOST_CHECK_EQUAL(json["method"].asString(), "setDeployAuthType(uint8)");
    BOOST_CHECK_EQUAL(json["codeName"].asString(), "NO_AUTHORIZED");
    BOOST_CHECK(!json["ok"].asBool());
    BOOST_CHECK_EQUAL(authSentTx(rpc).to, c_authAddress);
}

BOOST_AUTO_TEST_CASE(adminWritesStayDirectEvenOnAnAuthChain)
{
    struct Case
    {
        std::vector<std::string> argv;
        std::string signature;
    };
    for (auto const& c : std::vector<Case>{
             {{"set-method-auth-type", c_authContract, c_authSelector, "1"},
                 "setMethodAuthType(address,bytes4,uint8)"},
             {{"open-method-auth", c_authContract, c_authSelector, c_authAccount},
                 "openMethodAuth(address,bytes4,address)"},
             {{"close-method-auth", c_authContract, c_authSelector, c_authAccount},
                 "closeMethodAuth(address,bytes4,address)"},
         })
    {
        auto rpc = authChainRpc();
        rpc.set("sendTransaction", authReceipt(authOutput("int32", "[0]")));
        auto argv = c.argv;
        argv.insert(argv.end(), {"--yes", "--json"});
        std::ostringstream out;
        BOOST_CHECK_EQUAL(runAuthOn(authConnection(rpc), authArgs(argv), out), c_exitOk);
        auto json = parseJson(out.str(), c.signature);
        BOOST_CHECK_EQUAL(json["mode"].asString(), "direct");
        BOOST_CHECK_EQUAL(json["method"].asString(), c.signature);
        BOOST_CHECK_EQUAL(json["code"].asInt(), 0);
        auto tx = authSentTx(rpc);
        BOOST_CHECK_EQUAL(tx.to, c_authAddress);
        BOOST_CHECK_EQUAL(tx.input.substr(0, 8), authSel(c.signature));
        BOOST_CHECK(tx.input.find("4ed3885e000000000000000000000000000000000000000000000000") !=
                    std::string::npos);
    }
}

BOOST_AUTO_TEST_CASE(setContractStatusPicksTheOverload)
{
    for (auto const& [status, signature] : std::vector<std::pair<std::string, std::string>>{
             {"true", "setContractStatus(address,bool)"},
             {"false", "setContractStatus(address,bool)"},
             {"1", "setContractStatus(address,uint8)"}, {"2", "setContractStatus(address,uint8)"}})
    {
        FakeRpc rpc;
        rpc.set("sendTransaction", authReceipt(authOutput("int32", "[0]")));
        std::ostringstream out;
        BOOST_CHECK_EQUAL(
            runAuthOn(authConnection(rpc),
                authArgs({"set-contract-status", c_authContract, status, "--yes", "--json"}), out),
            c_exitOk);
        BOOST_CHECK_EQUAL(parseJson(out.str(), status)["method"].asString(), signature);
        BOOST_CHECK_EQUAL(authSentTx(rpc).input.substr(0, 8), authSel(signature));
    }
    FakeRpc rpc;
    std::ostringstream out;
    BOOST_CHECK_THROW(
        runAuthOn(authConnection(rpc),
            authArgs({"set-contract-status", c_authContract, "frozen", "--yes"}), out),
        OpsError);
}

BOOST_AUTO_TEST_CASE(usageAndHelp)
{
    FakeRpc rpc;
    std::ostringstream out;
    BOOST_CHECK_THROW(runAuthOn(authConnection(rpc), authArgs({"get-admin"}), out), OpsError);
    BOOST_CHECK_THROW(runAuthOn(authConnection(rpc), authArgs({"grant"}), out), OpsError);
    std::ostringstream help;
    std::ostringstream err;
    BOOST_CHECK_EQUAL(runOps({"fisco-bcos", "auth", "--help"}, help, err), c_exitOk);
    for (auto const* name : {"getContractAdmin", "checkDeployAuth", "getMethodAuth", "resetAdmin",
             "setDeployAuthType", "openMethodAuth", "freezeContract"})
    {
        BOOST_CHECK(help.str().find(name) != std::string::npos);
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
