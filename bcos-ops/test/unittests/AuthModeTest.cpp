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
 * @file AuthModeTest.cpp
 */
#include "bcos-ops/tx/AuthMode.h"
#include "fixtures/FakeRpc.h"
#include <boost/test/unit_test.hpp>

namespace bcos::ops::test
{
namespace
{
NodeDir genesisAuth(bool _on)
{
    NodeDir dir;
    dir.authCheck = _on;
    return dir;
}

Json::Value configValue(std::string const& _value)
{
    Json::Value value;
    value["value"] = _value;
    value["blockNumber"] = 3;
    return value;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(AuthModeTest)

BOOST_AUTO_TEST_CASE(systemConfigWinsOverGenesis)
{
    FakeRpc rpc;
    rpc.set("getSystemConfigByKey", configValue("1"));
    BOOST_CHECK(chainUsesProposals(rpc.call(), "group0", genesisAuth(false)));
    rpc.set("getSystemConfigByKey", configValue("0"));
    BOOST_CHECK(!chainUsesProposals(rpc.call(), "group0", genesisAuth(true)));
    BOOST_REQUIRE_EQUAL(rpc.calls().back().first, "getSystemConfigByKey");
    BOOST_CHECK_EQUAL(rpc.calls().back().second[2].asString(), "auth_check_status");
}

BOOST_AUTO_TEST_CASE(unsetKeyFallsBackToGenesis)
{
    FakeRpc rpc;  // getSystemConfigByKey answers 3008 by default
    BOOST_CHECK(chainUsesProposals(rpc.call(), "group0", genesisAuth(true)));
    BOOST_CHECK(!chainUsesProposals(rpc.call(), "group0", genesisAuth(false)));
    BOOST_CHECK(!chainUsesProposals(rpc.call(), "group0", std::nullopt));  // bare --rpc
}

BOOST_AUTO_TEST_CASE(transportFailureIsNotSwallowed)
{
    FakeRpc rpc;
    rpc.remove("getSystemConfigByKey");
    rpc.set("getBlockNumber", 1);  // keep the table otherwise intact
    rpc.fail("getSystemConfigByKey", 3008, "empty");
    BOOST_CHECK(!chainUsesProposals(rpc.call(), "group0", std::nullopt));
    FakeRpc down;
    down.remove("getSystemConfigByKey");
    down.set("getSystemConfigByKey", Json::Value());  // cleared failure, then no result at all
    down.remove("getSystemConfigByKey");
    BOOST_CHECK_THROW(chainUsesProposals(down.call(), "group0", std::nullopt), OpsError);
    // any JSON-RPC error other than 3008 (no row) is not "the key was never set"
    FakeRpc timeout;
    timeout.fail("getSystemConfigByKey", -32603, "request timed out");
    BOOST_CHECK_THROW(chainUsesProposals(timeout.call(), "group0", genesisAuth(true)), OpsError);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
