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
 * @brief `balance` group over a FakeRpc: encodings, uint256 decoding, receipt failures (the
 *        precompiled reverts instead of writing a code), register-caller's int32 code, the
 *        "run balance enable" hint, and enable = two feature flags (proposals on an auth chain)
 * @file BalanceCmdTest.cpp
 */
#include "bcos-ops/cmd/BalanceCmd.h"
#include "bcos-ops/Cli.h"
#include "bcos-ops/OpsError.h"
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
std::string const c_balAddress = "0x0000000000000000000000000000000000001011";
std::string const c_balCommittee = "0x0000000000000000000000000000000000010001";
std::string const c_balAccount = "0x3f12000000000000000000000000000000009a00";
std::string const c_balAccount2 = "0xf50500000000000000000000000000000043c100";

Connection balConnection(FakeRpc& _rpc)
{
    Connection connection;
    connection.call = _rpc.call();
    connection.source = "attach";
    connection.group = "group0";
    return connection;
}

Args balArgs(std::vector<std::string> _argv)
{
    return Args(std::move(_argv), {"json", "yes", "direct"});
}

std::string balSelector(std::string const& _signature)
{
    return bcos::crypto::Keccak256().hash(bytesConstRef(_signature)).hex().substr(0, 8);
}

/// ABI-encoded output of a call: encode a fake method with the output types, drop the selector
std::string balOutput(std::string const& _types, std::string const& _jsonValues)
{
    auto data = Abi(false).encodeMethodBySignature("o(" + _types + ")", _jsonValues);
    return "0x" + toHex(bytes(data.begin() + 4, data.end()));
}

Json::Value balReceipt(int _status, std::string const& _output, std::string const& _message = "")
{
    Json::Value receipt;
    receipt["status"] = _status;
    receipt["transactionHash"] = "0x9c";
    receipt["blockNumber"] = 129;
    receipt["gasUsed"] = "21000";
    receipt["output"] = _output;
    receipt["message"] = _message;
    return receipt;
}

Json::Value balCallResult(int _status, std::string const& _output)
{
    Json::Value value;
    value["status"] = _status;
    value["output"] = _output;
    return value;
}

struct SentTx
{
    std::string to;
    std::string selector;
    std::string input;
};

std::vector<SentTx> balSentTxs(FakeRpc const& _rpc)
{
    std::vector<SentTx> sent;
    for (auto const& [method, params] : _rpc.calls())
    {
        if (method == "sendTransaction")
        {
            bcos::cppsdk::utilities::TransactionBuilder builder;
            auto tx = builder.decodeTransaction(fromHex(params[2].asString()));
            auto input = toHex(tx->data.input);
            sent.push_back({tx->data.to, input.substr(0, 8), input});
        }
    }
    return sent;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(BalanceCmdTest)

BOOST_AUTO_TEST_CASE(getDecodesTheUint256AsDecimal)
{
    FakeRpc rpc;
    rpc.setCall(balSelector("getBalance(address)"),
        balCallResult(0, balOutput("uint256", "[\"1000000000000000000000\"]")));
    std::ostringstream out;
    BOOST_CHECK_EQUAL(
        runBalanceOn(balConnection(rpc), balArgs({"get", c_balAccount, "--json"}), out), c_exitOk);
    auto json = parseJson(out.str(), "get");
    BOOST_CHECK_EQUAL(json["method"].asString(), "getBalance(address)");
    BOOST_CHECK_EQUAL(json["to"].asString(), c_balAddress);
    BOOST_CHECK_EQUAL(json["value"].asString(), "1000000000000000000000");
    BOOST_CHECK(json["ok"].asBool());
    // the call carried the selector and the address
    bool seen = false;
    for (auto const& [method, params] : rpc.calls())
    {
        if (method == "call")
        {
            BOOST_CHECK_EQUAL(params[2].asString(), c_balAddress);
            BOOST_CHECK(params[3].asString().starts_with(balSelector("getBalance(address)")));
            seen = true;
        }
    }
    BOOST_CHECK(seen);
}

BOOST_AUTO_TEST_CASE(writesEncodeTheirSignatureAndArguments)
{
    struct Case
    {
        std::vector<std::string> argv;
        std::string signature;
    };
    for (auto const& c : std::vector<Case>{
             {{"add", c_balAccount, "500"}, "addBalance(address,uint256)"},
             {{"sub", c_balAccount, "7"}, "subBalance(address,uint256)"},
             {{"transfer", c_balAccount, c_balAccount2, "3"}, "transfer(address,address,uint256)"},
         })
    {
        FakeRpc rpc;
        rpc.set("sendTransaction", balReceipt(0, "0x"));
        auto argv = c.argv;
        argv.insert(argv.end(), {"--yes", "--json"});
        std::ostringstream out;
        BOOST_CHECK_EQUAL(runBalanceOn(balConnection(rpc), balArgs(argv), out), c_exitOk);
        auto json = parseJson(out.str(), c.signature);
        BOOST_CHECK_EQUAL(json["method"].asString(), c.signature);
        BOOST_CHECK_EQUAL(json["mode"].asString(), "direct");
        BOOST_CHECK(json["ok"].asBool());
        BOOST_CHECK(json["code"].isNull());  // no int32 output: ok follows the receipt
        auto sent = balSentTxs(rpc);
        BOOST_REQUIRE_EQUAL(sent.size(), 1U);
        BOOST_CHECK_EQUAL(sent[0].to, c_balAddress);
        BOOST_CHECK_EQUAL(sent[0].selector, balSelector(c.signature));
        // the amount is the last 32-byte word
        BOOST_CHECK_EQUAL(
            sent[0].input.substr(sent[0].input.size() - 2), c.argv.back() == "500" ? "f4" :
                                                            c.argv.back() == "7"   ? "07" :
                                                                                     "03");
    }
}

BOOST_AUTO_TEST_CASE(revertedReceiptFailsWithTheMessage)
{
    FakeRpc rpc;
    rpc.set("sendTransaction", balReceipt(15, "0x", "only caller can addBalance"));
    std::ostringstream out;
    auto code = runBalanceOn(
        balConnection(rpc), balArgs({"add", c_balAccount, "500", "--yes", "--json"}), out);
    BOOST_CHECK_EQUAL(code, c_exitChecksFailed);
    auto json = parseJson(out.str(), "add");
    BOOST_CHECK(!json["ok"].asBool());
    BOOST_CHECK_EQUAL(json["status"].asInt(), 15);
    BOOST_CHECK_EQUAL(json["message"].asString(), "only caller can addBalance");
    BOOST_CHECK(json["hint"].isNull());
}

BOOST_AUTO_TEST_CASE(unregisteredPrecompiledHintsAtEnable)
{
    FakeRpc rpc;
    rpc.set("sendTransaction", balReceipt(19, "0x"));  // CallAddressError
    std::ostringstream out;
    BOOST_CHECK_EQUAL(runBalanceOn(balConnection(rpc),
                          balArgs({"add", c_balAccount, "500", "--yes", "--json"}), out),
        c_exitChecksFailed);
    BOOST_CHECK(
        parseJson(out.str(), "add")["hint"].asString().find("balance enable") != std::string::npos);
    rpc.setCall(balSelector("getBalance(address)"), balCallResult(19, "0x"));
    std::ostringstream read;
    BOOST_CHECK_EQUAL(
        runBalanceOn(balConnection(rpc), balArgs({"get", c_balAccount, "--json"}), read),
        c_exitChecksFailed);
    BOOST_CHECK(parseJson(read.str(), "get")["hint"].asString().find("balance enable") !=
                std::string::npos);
}

BOOST_AUTO_TEST_CASE(registerCallerReturnsAnInt32Code)
{
    FakeRpc rpc;
    rpc.set("sendTransaction", balReceipt(0, balOutput("int32", "[0]")));
    std::ostringstream out;
    BOOST_CHECK_EQUAL(runBalanceOn(balConnection(rpc),
                          balArgs({"register-caller", c_balAccount, "--yes", "--json"}), out),
        c_exitOk);
    auto json = parseJson(out.str(), "register-caller");
    BOOST_CHECK_EQUAL(json["method"].asString(), "registerCaller(address)");
    BOOST_CHECK_EQUAL(json["code"].asInt(), 0);
    BOOST_CHECK(json["ok"].asBool());
    FakeRpc denied;
    denied.set("sendTransaction", balReceipt(0, balOutput("int32", "[-50000]")));
    std::ostringstream err;
    BOOST_CHECK_EQUAL(runBalanceOn(balConnection(denied),
                          balArgs({"unregister-caller", c_balAccount, "--yes", "--json"}), err),
        c_exitChecksFailed);
    auto deniedJson = parseJson(err.str(), "unregister-caller");
    BOOST_CHECK_EQUAL(deniedJson["method"].asString(), "unregisterCaller(address)");
    BOOST_CHECK_EQUAL(deniedJson["codeName"].asString(), "NO_AUTHORIZED");
}

BOOST_AUTO_TEST_CASE(listCallerDecodesTheAddressArray)
{
    FakeRpc rpc;
    rpc.setCall(balSelector("listCaller()"),
        balCallResult(
            0, balOutput("address[]", "[[\"" + c_balAccount + "\",\"" + c_balAccount2 + "\"]]")));
    std::ostringstream out;
    BOOST_CHECK_EQUAL(
        runBalanceOn(balConnection(rpc), balArgs({"list-caller", "--json"}), out), c_exitOk);
    auto json = parseJson(out.str(), "list-caller");
    BOOST_REQUIRE(json["value"].isArray());
    BOOST_REQUIRE_EQUAL(json["value"].size(), 2U);
    BOOST_CHECK_EQUAL(json["value"][1].asString(), c_balAccount2);
}

BOOST_AUTO_TEST_CASE(enableTurnsOnBothFlagsInOrder)
{
    FakeRpc rpc;  // getSystemConfigByKey answers 3008: both flags off
    rpc.set("sendTransaction", balReceipt(0, balOutput("int32", "[0]")));
    std::ostringstream out;
    BOOST_CHECK_EQUAL(
        runBalanceOn(balConnection(rpc), balArgs({"enable", "--yes", "--json"}), out), c_exitOk);
    auto json = parseJson(out.str(), "enable");
    BOOST_CHECK(json["ok"].asBool());
    BOOST_CHECK_EQUAL(json["mode"].asString(), "direct");
    BOOST_REQUIRE_EQUAL(json["steps"].size(), 2U);
    BOOST_CHECK_EQUAL(json["steps"][0]["name"].asString(), "feature_balance");
    BOOST_CHECK_EQUAL(json["steps"][0]["action"].asString(), "enabled");
    BOOST_CHECK_EQUAL(json["steps"][1]["name"].asString(), "feature_balance_precompiled");
    BOOST_CHECK(json["hint"].asString().find("register-caller") != std::string::npos);
    auto sent = balSentTxs(rpc);
    BOOST_REQUIRE_EQUAL(sent.size(), 2U);
    BOOST_CHECK_EQUAL(sent[0].to, "0x0000000000000000000000000000000000001000");
    BOOST_CHECK_EQUAL(sent[0].selector, balSelector("setValueByKey(string,string)"));
}

BOOST_AUTO_TEST_CASE(enableOnAnAuthChainSendsTwoProposals)
{
    FakeRpc rpc;
    Json::Value authOn;
    authOn["value"] = "1";
    authOn["blockNumber"] = 2;
    rpc.set("getSystemConfigByKey", authOn);  // auth_check_status = 1; flags read as "1"? no:
    // the flags must read as off, so answer by key: auth on, flags missing
    rpc.fail("getSystemConfigByKey", 3008, "empty entry");
    rpc.queue("getSystemConfigByKey", authOn);  // first read: auth_check_status
    auto create = balReceipt(0, balOutput("uint256", "[7]"));
    create["logEntries"] = Json::Value(Json::arrayValue);
    rpc.set("sendTransaction", create);
    rpc.setCall(balSelector("_proposalMgr()"),
        balCallResult(0, balOutput("address", "[\"" + c_balAccount2 + "\"]")));
    rpc.setCall(
        balSelector("getProposalStatus(uint256)"), balCallResult(0, balOutput("uint8", "[2]")));
    rpc.setCall(balSelector("getProposalInfo(uint256)"),
        balCallResult(0, balOutput("address,address,uint8,uint256,uint8,address[],address[]",
                             "[\"" + c_balAddress + "\",\"" + c_balAccount + "\",51,1128,2,[\"" +
                                 c_balAccount + "\"],[]]")));
    std::ostringstream out;
    BOOST_CHECK_EQUAL(
        runBalanceOn(balConnection(rpc), balArgs({"enable", "--yes", "--json"}), out), c_exitOk);
    auto json = parseJson(out.str(), "enable");
    BOOST_CHECK_EQUAL(json["mode"].asString(), "proposal");
    BOOST_REQUIRE_EQUAL(json["steps"].size(), 2U);
    BOOST_CHECK_EQUAL(json["steps"][1]["result"]["proposal"]["statusName"].asString(), "passed");
    auto sent = balSentTxs(rpc);
    BOOST_REQUIRE_EQUAL(sent.size(), 2U);
    for (auto const& tx : sent)
    {
        BOOST_CHECK_EQUAL(tx.to, c_balCommittee);
        BOOST_CHECK_EQUAL(
            tx.selector, balSelector("createSetSysConfigProposal(string,string,uint256)"));
    }
}

BOOST_AUTO_TEST_CASE(usageAndHelp)
{
    FakeRpc rpc;
    std::ostringstream out;
    BOOST_CHECK_THROW(
        runBalanceOn(balConnection(rpc), balArgs({"add", c_balAccount}), out), OpsError);
    BOOST_CHECK_THROW(runBalanceOn(balConnection(rpc), balArgs({"mint"}), out), OpsError);
    std::ostringstream help;
    std::ostringstream err;
    BOOST_CHECK_EQUAL(runOps({"fisco-bcos", "balance", "--help"}, help, err), c_exitOk);
    for (auto const* name : {"getBalance", "addBalance", "subBalance", "registerBalanceGovernor",
             "listBalanceGovernor"})
    {
        BOOST_CHECK(help.str().find(name) != std::string::npos);
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
