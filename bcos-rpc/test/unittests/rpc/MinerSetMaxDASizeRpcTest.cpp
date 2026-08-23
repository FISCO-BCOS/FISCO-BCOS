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
 * @file MinerSetMaxDASizeRpcTest.cpp
 * @brief miner_setMaxDASize endpoint shape tests (OP Stack batcher DA throttling)
 */

#include "../common/RPCFixture.h"
#include <bcos-rpc/web3jsonrpc/endpoints/EndpointsMapping.h>
#include <boost/test/unit_test.hpp>
#include <future>
#include <string>
#include <vector>

namespace bcos::test
{

class MinerSetMaxDASizeFixture : public RPCFixture
{
public:
    MinerSetMaxDASizeFixture()
    {
        rpc = factory->buildLocalRpc(groupInfo, nodeService);
        web3JsonRpc = rpc->web3JsonRpc();
        BOOST_TEST(web3JsonRpc != nullptr);
    }

    Json::Value call(std::string const& method, std::vector<std::string> const& params)
    {
        Json::Value req;
        req["jsonrpc"] = "2.0";
        req["id"] = 1;
        req["method"] = method;
        Json::Value args(Json::arrayValue);
        for (auto const& param : params)
        {
            args.append(param);
        }
        req["params"] = args;
        std::promise<bcos::bytes> promise;
        web3JsonRpc->onRPCRequest(printJson(req),
            [&promise](bcos::bytes resp, boost::beast::http::status) {
                promise.set_value(std::move(resp));
            });
        auto const raw = promise.get_future().get();
        std::string_view json(reinterpret_cast<char const*>(raw.data()), raw.size());
        Json::Value out;
        Json::Reader reader;
        reader.parse(json.begin(), json.end(), out);
        return out;
    }

    Rpc::Ptr rpc;
    Web3JsonRpcImpl::Ptr web3JsonRpc;
};

BOOST_FIXTURE_TEST_SUITE(MinerSetMaxDASizeRpcTest, MinerSetMaxDASizeFixture)

// The batcher's throttling loop treats "method not found" as FATAL ("either enable it or
// disable throttling", op-batcher driver.go:631) — the method must be registered and answer.
BOOST_AUTO_TEST_CASE(MethodRegistered)
{
    EndpointsMapping const mapping;
    BOOST_CHECK(mapping.findHandler("miner_setMaxDASize").has_value());
}

// Two 0x quantities in, `true` back; a throttling batcher re-pushes updated caps
// periodically, so repeated calls must keep succeeding (update path).
BOOST_AUTO_TEST_CASE(AcceptsAndUpdatesCaps)
{
    auto resp = call("miner_setMaxDASize", {"0x1000", "0x400000"});
    BOOST_TEST(!resp.isMember("error"));
    BOOST_TEST(resp["result"].asBool());

    resp = call("miner_setMaxDASize", {"0x2000", "0x800000"});
    BOOST_TEST(!resp.isMember("error"));
    BOOST_TEST(resp["result"].asBool());
}

// Missing or malformed quantities are InvalidParams, never a silently-reset cap (a null
// param would otherwise parse as 0 = "no throttle" and un-throttle a throttled batcher).
BOOST_AUTO_TEST_CASE(MalformedParamsAreInvalidParams)
{
    auto oneParam = call("miner_setMaxDASize", {"0x1000"});
    BOOST_REQUIRE(oneParam.isMember("error"));
    BOOST_CHECK_EQUAL(oneParam["error"]["code"].asInt(), -32602);

    auto garbage = call("miner_setMaxDASize", {"0x1000", "not-hex"});
    BOOST_REQUIRE(garbage.isMember("error"));
    BOOST_CHECK_EQUAL(garbage["error"]["code"].asInt(), -32602);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
