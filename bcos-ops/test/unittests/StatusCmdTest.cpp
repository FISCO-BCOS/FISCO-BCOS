/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include "fixtures/FakeRpc.h"
#include <bcos-ops/Connect.h>
#include <bcos-ops/cmd/StatusCmd.h>
#include <boost/test/unit_test.hpp>
#include <sstream>

namespace bcos::ops::test
{
namespace
{
Connection fakeConnection(FakeRpc& _rpc)
{
    Connection connection;
    connection.call = _rpc.call();
    connection.source = "rpc";
    connection.group = "group0";
    connection.endpoint = "127.0.0.1:20200";
    return connection;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(StatusCmdTest)

BOOST_AUTO_TEST_CASE(jsonOutputHasSourceAndFiveChecks)
{
    FakeRpc rpc;
    std::ostringstream out;
    auto code = runStatusOn(fakeConnection(rpc), Thresholds{}, true, out);
    BOOST_CHECK_EQUAL(code, c_exitOk);
    auto json = parseJson(out.str(), "status");
    BOOST_CHECK_EQUAL(json["source"].asString(), "rpc");
    BOOST_REQUIRE(json["checks"].isArray());
    BOOST_CHECK_EQUAL(json["checks"].size(), 5U);
    BOOST_CHECK_EQUAL(json["chain"]["blockNumber"].asInt64(), 128);
    BOOST_CHECK_EQUAL(json["checks"][1]["name"].asString(), "consensus_stable");
    BOOST_CHECK(json["checks"][1]["ok"].asBool());
    BOOST_CHECK(!json["checks"][1]["inTimeout"].asBool());
}

BOOST_AUTO_TEST_CASE(inTimeoutExitsTwo)
{
    FakeRpc rpc;
    rpc.setConsensus("timeout", true);
    std::ostringstream out;
    auto code = runStatusOn(fakeConnection(rpc), Thresholds{}, true, out);
    BOOST_CHECK_EQUAL(code, c_exitChecksFailed);
    auto json = parseJson(out.str(), "status");
    BOOST_CHECK(!json["checks"][1]["ok"].asBool());
    BOOST_CHECK(json["checks"][1]["inTimeout"].asBool());
}

BOOST_AUTO_TEST_CASE(tableOutputMentionsFailedCheck)
{
    FakeRpc rpc;
    rpc.setConsensus("timeout", true);
    std::ostringstream out;
    auto code = runStatusOn(fakeConnection(rpc), Thresholds{}, false, out);
    BOOST_CHECK_EQUAL(code, c_exitChecksFailed);
    BOOST_CHECK(out.str().find("1 fail: consensus_stable") != std::string::npos);
    BOOST_CHECK(out.str().find("height 128") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(parseHostPortForms)
{
    auto [host, port] = parseHostPort("127.0.0.1:20201");
    BOOST_CHECK_EQUAL(host, "127.0.0.1");
    BOOST_CHECK_EQUAL(port, 20201);
    auto [host2, port2] = parseHostPort("ws://node.example:20200");
    BOOST_CHECK_EQUAL(host2, "node.example");
    BOOST_CHECK_EQUAL(port2, 20200);
    BOOST_CHECK_THROW(parseHostPort("127.0.0.1"), OpsError);
    BOOST_CHECK_THROW(parseHostPort("127.0.0.1:99999"), OpsError);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
