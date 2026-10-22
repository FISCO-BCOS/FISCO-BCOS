/*
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
 * @file Web3BatchRequestTest.cpp
 * @brief Regression test for issue #5706: a JSON-RPC batch with exactly one call must be
 *        answered with a one-element array, not a bare response object (JSON-RPC 2.0 §6).
 *        ethers.js v6 sends its network-detection eth_chainId as a one-element batch and cannot
 *        match a bare object, so it never starts against the node.
 */

#include "../common/RPCFixture.h"
#include <bcos-rpc/web3jsonrpc/Web3JsonRpcImpl.h>
#include <boost/test/unit_test.hpp>
#include <future>
#include <string_view>

using namespace bcos;
using namespace bcos::rpc;

namespace bcos::test
{

class Web3BatchRequestFixture : public RPCFixture
{
public:
    Web3BatchRequestFixture()
    {
        rpc = factory->buildLocalRpc(groupInfo, nodeService);
        web3JsonRpc = rpc->web3JsonRpc();
        BOOST_REQUIRE(web3JsonRpc != nullptr);
    }

    Json::Value request(std::string_view req)
    {
        std::promise<bcos::bytes> promise;
        web3JsonRpc->onRPCRequest(req, [&promise](bcos::bytes resp, boost::beast::http::status) {
            promise.set_value(std::move(resp));
        });
        auto jsonBytes = promise.get_future().get();
        Json::Value value;
        Json::Reader reader;
        std::string_view json((char*)jsonBytes.data(), jsonBytes.size());
        BOOST_REQUIRE(reader.parse(json.begin(), json.end(), value));
        return value;
    }

    Rpc::Ptr rpc;
    Web3JsonRpcImpl::Ptr web3JsonRpc;
};

BOOST_FIXTURE_TEST_SUITE(Web3BatchRequestTest, Web3BatchRequestFixture)

// The regression: `[{...}]` is a batch and is answered with `[{...}]`.
BOOST_AUTO_TEST_CASE(oneElementBatchIsAnsweredWithAnArray)
{
    auto resp = request(R"([{"jsonrpc":"2.0","id":7,"method":"eth_chainId","params":[]}])");
    BOOST_REQUIRE(resp.isArray());
    BOOST_REQUIRE_EQUAL(resp.size(), 1U);
    BOOST_CHECK_EQUAL(resp[0U]["id"].asInt(), 7);
    BOOST_CHECK_EQUAL(resp[0U]["jsonrpc"].asString(), "2.0");
    BOOST_CHECK(resp[0U].isMember("result"));
}

// Control: a plain (non-batch) request object is still answered with a bare object.
BOOST_AUTO_TEST_CASE(singleObjectRequestIsAnsweredWithAnObject)
{
    auto resp = request(R"({"jsonrpc":"2.0","id":8,"method":"eth_chainId","params":[]})");
    BOOST_REQUIRE(resp.isObject());
    BOOST_CHECK_EQUAL(resp["id"].asInt(), 8);
    BOOST_CHECK(resp.isMember("result"));
}

// Control: a multi-element batch keeps its array shape and per-request ids / order.
BOOST_AUTO_TEST_CASE(multiElementBatchKeepsOrderAndIds)
{
    auto resp = request(R"([{"jsonrpc":"2.0","id":1,"method":"eth_chainId","params":[]},)"
                        R"({"jsonrpc":"2.0","id":2,"method":"eth_blockNumber","params":[]}])");
    BOOST_REQUIRE(resp.isArray());
    BOOST_REQUIRE_EQUAL(resp.size(), 2U);
    BOOST_CHECK_EQUAL(resp[0U]["id"].asInt(), 1);
    BOOST_CHECK_EQUAL(resp[1U]["id"].asInt(), 2);
    BOOST_CHECK(resp[0U].isMember("result"));
    BOOST_CHECK(resp[1U].isMember("result"));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
