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
 * @file ConsensusCmdTest.cpp
 */
#include "bcos-ops/Cli.h"
#include "bcos-ops/OpsError.h"
#include "bcos-ops/cmd/SysconfigCmd.h"
#include "fixtures/FakeRpc.h"
#include <boost/test/unit_test.hpp>
#include <sstream>

namespace bcos::ops::test
{
namespace
{
Connection consensusConnection(FakeRpc& _rpc)
{
    Connection connection;
    connection.call = _rpc.call();
    connection.source = "attach";
    connection.group = "group0";
    return connection;
}

Json::Value consensusReceipt(std::string const& _output)
{
    Json::Value receipt;
    receipt["status"] = 0;
    receipt["transactionHash"] = "0x9c";
    receipt["blockNumber"] = 129;
    receipt["gasUsed"] = "21000";
    receipt["output"] = _output;
    return receipt;
}

std::string const c_consCodeZero =
    "0x0000000000000000000000000000000000000000000000000000000000000000";
std::string const c_consCodeObserverFirst =  // int32(-51104)
    "0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff3860";
std::string const c_consNode3 = "74637d36";

Args consArgs(std::vector<std::string> _argv)
{
    return Args(std::move(_argv), {"json", "yes"});
}

FakeRpc consensusRpc()
{
    FakeRpc rpc;
    Json::Value sealers(Json::arrayValue);
    for (auto const& id : {"3a1f00", "3a1f01", "3a1f02"})
    {
        Json::Value sealer;
        sealer["nodeID"] = id;
        sealer["weight"] = 1;
        sealer["termWeight"] = 0;
        sealers.append(sealer);
    }
    rpc.set("getSealerList", sealers);
    Json::Value observers(Json::arrayValue);
    observers.append(c_consNode3);
    rpc.set("getObserverList", observers);
    return rpc;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(ConsensusCmdTest)

BOOST_AUTO_TEST_CASE(listMergesSealersAndObservers)
{
    auto rpc = consensusRpc();
    std::ostringstream out;
    BOOST_CHECK_EQUAL(
        runConsensusOn(consensusConnection(rpc), consArgs({"list", "--json"}), out), c_exitOk);
    auto json = parseJson(out.str(), "list");
    BOOST_REQUIRE_EQUAL(json.size(), 4U);
    BOOST_CHECK_EQUAL(json[0]["role"].asString(), "sealer");
    BOOST_CHECK_EQUAL(json[0]["weight"].asInt(), 1);
    BOOST_CHECK_EQUAL(json[3]["role"].asString(), "observer");
    BOOST_CHECK_EQUAL(json[3]["nodeId"].asString(), c_consNode3);
    std::ostringstream table;
    runConsensusOn(consensusConnection(rpc), consArgs({"list"}), table);
    BOOST_CHECK(table.str().find("observer") != std::string::npos);
    BOOST_CHECK(table.str().find(c_consNode3) != std::string::npos);
}

BOOST_AUTO_TEST_CASE(addSealerEncodesTwoOrThreeArguments)
{
    auto rpc = consensusRpc();
    rpc.set("sendTransaction", consensusReceipt(c_consCodeZero));
    std::ostringstream out;
    auto code = runConsensusOn(consensusConnection(rpc),
        consArgs({"add-sealer", c_consNode3, "2", "--yes", "--json"}), out);
    BOOST_CHECK_EQUAL(code, c_exitOk);
    auto json = parseJson(out.str(), "add-sealer");
    BOOST_CHECK_EQUAL(json["method"].asString(), "addSealer(string,uint256)");
    BOOST_CHECK_EQUAL(json["to"].asString(), "0x0000000000000000000000000000000000001003");
    BOOST_CHECK_EQUAL(json["params"][0].asString(), c_consNode3);
    BOOST_CHECK_EQUAL(json["params"][1].asInt(), 2);
    BOOST_CHECK_EQUAL(json["mode"].asString(), "direct");
    BOOST_CHECK(json["ok"].asBool());

    std::ostringstream term;
    runConsensusOn(consensusConnection(rpc),
        consArgs({"add-sealer", c_consNode3, "2", "--term-weight", "5", "--yes", "--json"}), term);
    auto termJson = parseJson(term.str(), "add-sealer-term");
    BOOST_CHECK_EQUAL(termJson["method"].asString(), "addSealer(string,uint256,uint256)");
    BOOST_CHECK_EQUAL(termJson["params"][2].asInt(), 5);
}

BOOST_AUTO_TEST_CASE(negativeCodeNamesTheReasonAndExitsTwo)
{
    auto rpc = consensusRpc();
    rpc.set("sendTransaction", consensusReceipt(c_consCodeObserverFirst));
    std::ostringstream out;
    auto code = runConsensusOn(
        consensusConnection(rpc), consArgs({"add-sealer", "3a1f00", "1", "--yes", "--json"}), out);
    BOOST_CHECK_EQUAL(code, c_exitChecksFailed);
    auto json = parseJson(out.str(), "add-sealer");
    BOOST_CHECK_EQUAL(json["status"].asInt(), 0);
    BOOST_CHECK_EQUAL(json["code"].asInt(), -51104);
    BOOST_CHECK_EQUAL(json["codeName"].asString(), "SEALER_MUST_BE_OBSERVER_FIRST");
    BOOST_CHECK(!json["ok"].asBool());
}

BOOST_AUTO_TEST_CASE(everyWriteSubcommandHitsItsSignature)
{
    auto rpc = consensusRpc();
    rpc.set("sendTransaction", consensusReceipt(c_consCodeZero));
    struct Case
    {
        std::vector<std::string> argv;
        std::string signature;
    };
    for (auto const& c : std::vector<Case>{
             {{"add-observer", c_consNode3}, "addObserver(string)"},
             {{"remove", c_consNode3}, "remove(string)"},
             {{"set-weight", c_consNode3, "3"}, "setWeight(string,uint256)"},
             {{"set-term-weight", c_consNode3, "3"}, "setTermWeight(string,uint256)"},
         })
    {
        auto argv = c.argv;
        argv.insert(argv.end(), {"--yes", "--json"});
        std::ostringstream out;
        BOOST_CHECK_EQUAL(runConsensusOn(consensusConnection(rpc), consArgs(argv), out), c_exitOk);
        BOOST_CHECK_EQUAL(parseJson(out.str(), c.signature)["method"].asString(), c.signature);
    }
    std::ostringstream err;
    BOOST_CHECK_THROW(
        runConsensusOn(consensusConnection(rpc), consArgs({"remove"}), err), OpsError);
    BOOST_CHECK_THROW(
        runConsensusOn(consensusConnection(rpc), consArgs({"rotate"}), err), OpsError);
}

BOOST_AUTO_TEST_CASE(helpNamesConsoleCommands)
{
    std::ostringstream out;
    std::ostringstream err;
    BOOST_CHECK_EQUAL(runOps({"fisco-bcos", "consensus", "--help"}, out, err), c_exitOk);
    for (auto const* name : {"addSealer", "addObserver", "removeNode", "setConsensusWeight",
             "getSealerList", "getObserverList"})
    {
        BOOST_CHECK_MESSAGE(out.str().find(name) != std::string::npos, name);
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
