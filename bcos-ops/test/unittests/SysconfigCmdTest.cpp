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
 * @file SysconfigCmdTest.cpp
 */
#include "bcos-ops/cmd/SysconfigCmd.h"
#include "bcos-ops/Cli.h"
#include "bcos-ops/OpsError.h"
#include "fixtures/FakeRpc.h"
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <sstream>

namespace bcos::ops::test
{
namespace
{
Connection sysconfigConnection(FakeRpc& _rpc)
{
    Connection connection;
    connection.call = _rpc.call();
    connection.source = "rpc";
    connection.group = "group0";
    connection.endpoint = "127.0.0.1:20200";
    return connection;
}

Json::Value sysconfigReceipt(std::string const& _output, int _status = 0)
{
    Json::Value receipt;
    receipt["status"] = _status;
    receipt["transactionHash"] = "0x9c";
    receipt["blockNumber"] = 129;
    receipt["gasUsed"] = "21000";
    receipt["output"] = _output;
    return receipt;
}

std::string const c_sysCodeZero =
    "0x0000000000000000000000000000000000000000000000000000000000000000";
std::string const c_sysCodeNoAuth =  // int32(-50000)
    "0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff3cb0";

Args sysArgs(std::vector<std::string> _argv)
{
    return Args(std::move(_argv), {"json", "yes"});
}
}  // namespace

BOOST_AUTO_TEST_SUITE(SysconfigCmdTest)

BOOST_AUTO_TEST_CASE(getPrintsValueAndEnableNumber)
{
    FakeRpc rpc;
    Json::Value config;
    config["value"] = "1000";
    config["blockNumber"] = 54;
    rpc.set("getSystemConfigByKey", config);
    std::ostringstream out;
    auto code =
        runSysconfigOn(sysconfigConnection(rpc), sysArgs({"get", "tx_count_limit", "--json"}), out);
    BOOST_CHECK_EQUAL(code, c_exitOk);
    auto json = parseJson(out.str(), "get");
    BOOST_CHECK_EQUAL(json["key"].asString(), "tx_count_limit");
    BOOST_CHECK_EQUAL(json["value"].asString(), "1000");
    BOOST_CHECK_EQUAL(json["enableNumber"].asInt(), 54);
    BOOST_CHECK_EQUAL(rpc.calls().back().second[2].asString(), "tx_count_limit");
}

BOOST_AUTO_TEST_CASE(setSendsToSysConfigAndReportsDirect)
{
    FakeRpc rpc;
    rpc.set("sendTransaction", sysconfigReceipt(c_sysCodeZero));
    std::ostringstream out;
    auto code = runSysconfigOn(sysconfigConnection(rpc),
        sysArgs({"set", "tx_count_limit", "2000", "--yes", "--json"}), out);
    BOOST_CHECK_EQUAL(code, c_exitOk);
    auto json = parseJson(out.str(), "set");
    BOOST_CHECK_EQUAL(json["mode"].asString(), "direct");
    BOOST_CHECK_EQUAL(json["to"].asString(), "0x0000000000000000000000000000000000001000");
    BOOST_CHECK_EQUAL(json["method"].asString(), "setValueByKey(string,string)");
    BOOST_CHECK_EQUAL(json["params"][1].asString(), "2000");
    BOOST_CHECK_EQUAL(json["code"].asInt(), 0);
    BOOST_CHECK_EQUAL(json["codeName"].asString(), "SUCCESS");
    BOOST_CHECK(json["ok"].asBool());
    BOOST_CHECK_EQUAL(json["txHash"].asString(), "0x9c");
    bool sent = false;
    for (auto const& [method, params] : rpc.calls())
    {
        sent = sent || method == "sendTransaction";
    }
    BOOST_CHECK(sent);
}

BOOST_AUTO_TEST_CASE(setWithNegativeCodeExitsTwo)
{
    FakeRpc rpc;
    rpc.set("sendTransaction", sysconfigReceipt(c_sysCodeNoAuth));
    std::ostringstream out;
    auto code = runSysconfigOn(sysconfigConnection(rpc),
        sysArgs({"set", "tx_count_limit", "2000", "--yes", "--json"}), out);
    BOOST_CHECK_EQUAL(code, c_exitChecksFailed);
    auto json = parseJson(out.str(), "set");
    BOOST_CHECK_EQUAL(json["status"].asInt(), 0);  // the receipt itself succeeded
    BOOST_CHECK_EQUAL(json["code"].asInt(), -50000);
    BOOST_CHECK_EQUAL(json["codeName"].asString(), "NO_AUTHORIZED");
    BOOST_CHECK(!json["ok"].asBool());
}

BOOST_AUTO_TEST_CASE(featuresListsEveryFlag)
{
    FakeRpc rpc;
    Json::Value on;
    on["value"] = "1";
    on["blockNumber"] = 0;
    rpc.set("getSystemConfigByKey", on);
    std::ostringstream out;
    BOOST_CHECK_EQUAL(
        runSysconfigOn(sysconfigConnection(rpc), sysArgs({"features", "--json"}), out), c_exitOk);
    auto json = parseJson(out.str(), "features");
    BOOST_REQUIRE(json.isArray());
    BOOST_CHECK(json.size() > 50);
    BOOST_CHECK(json[0]["enabled"].asBool());
    // never set → every flag reads off (the node's "no row" error; a transport failure must not)
    rpc.fail("getSystemConfigByKey", 3008, "getSystemConfig failed for empty entry");
    std::ostringstream off;
    runSysconfigOn(sysconfigConnection(rpc), sysArgs({"features", "--json"}), off);
    BOOST_CHECK(!parseJson(off.str(), "features")[0]["enabled"].asBool());
}

BOOST_AUTO_TEST_CASE(enableFeaturesOrdersAndSkips)
{
    FakeRpc rpc;
    rpc.remove("getSystemConfigByKey");  // nothing on yet
    rpc.set("sendTransaction", sysconfigReceipt(c_sysCodeZero));
    std::ostringstream out;
    auto code = runSysconfigOn(sysconfigConnection(rpc),
        sysArgs({"enable-features", "feature_balance_precompiled", "--yes", "--json"}), out);
    BOOST_CHECK_EQUAL(code, c_exitOk);
    auto json = parseJson(out.str(), "enable");
    BOOST_CHECK(json["ok"].asBool());
    BOOST_REQUIRE_EQUAL(json["steps"].size(), 2U);
    BOOST_CHECK_EQUAL(json["steps"][0]["name"].asString(), "feature_balance");
    BOOST_CHECK_EQUAL(json["steps"][0]["action"].asString(), "enabled");
    BOOST_CHECK_EQUAL(json["steps"][1]["name"].asString(), "feature_balance_precompiled");
    BOOST_CHECK_EQUAL(
        json["steps"][1]["result"]["params"][0].asString(), "feature_balance_precompiled");
    std::ostringstream err;
    BOOST_CHECK_THROW(runSysconfigOn(sysconfigConnection(rpc),
                          sysArgs({"enable-features", "feature_nope", "--yes"}), err),
        OpsError);
    BOOST_CHECK_THROW(runSysconfigOn(sysconfigConnection(rpc), sysArgs({"bogus"}), err), OpsError);
}

BOOST_AUTO_TEST_CASE(helpNamesConsoleCommands)
{
    std::ostringstream out;
    std::ostringstream err;
    BOOST_CHECK_EQUAL(runOps({"fisco-bcos", "sysconfig", "--help"}, out, err), c_exitOk);
    BOOST_CHECK(out.str().find("getSystemConfigByKey") != std::string::npos);
    BOOST_CHECK(out.str().find("setSystemConfigByKey") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(writeWithoutYesIsRefusedWhenStdinIsNotATerminal)
{
    // ctest / CI pipes stdin; a governance write must then be explicit about skipping the prompt
    FakeRpc rpc;
    auto connection = sysconfigConnection(rpc);
    std::ostringstream out;
    BOOST_CHECK_EXCEPTION(
        runSysconfigOn(connection, sysArgs({"set", "tx_count_limit", "2000", "--json"}), out),
        OpsError, [](OpsError const& e) {
            return e.exitCode == c_exitUsage &&
                   std::string(e.what()).find("--yes") != std::string::npos;
        });
    BOOST_CHECK(rpc.calls().empty() || std::ranges::none_of(rpc.calls(), [](auto const& c) {
        return c.first == "sendTransaction";
    }));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
