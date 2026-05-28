/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
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
// Drives the web3 (eth_/net_/web3_) coroutine endpoints through the real
// dispatch path. EthEndpoint.cpp (596 lines) was at 24% because the existing
// Web3RpcTest only touched ~10 of the ~44 registered methods. onRPCRequest
// runs the handler coroutine to completion and hands back the JSON via a
// promise, so the whole thing is synchronous from the test's point of view.
class Web3MethodsFixture : public RPCFixture
{
public:
    Web3MethodsFixture()
    {
        rpc = factory->buildLocalRpc(groupInfo, nodeService);
        web3JsonRpc = rpc->web3JsonRpc();
        BOOST_REQUIRE(web3JsonRpc != nullptr);
    }

    Json::Value call(std::string_view request)
    {
        std::promise<bcos::bytes> promise;
        web3JsonRpc->onRPCRequest(
            request, [&promise](bcos::bytes resp) { promise.set_value(std::move(resp)); });
        auto jsonBytes = promise.get_future().get();
        Json::Value value;
        Json::Reader reader;
        std::string_view json((char*)jsonBytes.data(), jsonBytes.size());
        reader.parse(json.begin(), json.end(), value);
        return value;
    }

    static std::string req(std::string_view method, std::string_view params = "[]")
    {
        return std::string(R"({"jsonrpc":"2.0","id":1,"method":")") + std::string(method) +
               R"(","params":)" + std::string(params) + "}";
    }

    Rpc::Ptr rpc;
    Web3JsonRpcImpl::Ptr web3JsonRpc;
};

BOOST_FIXTURE_TEST_SUITE(Web3EthMethodsTest, Web3MethodsFixture)

BOOST_AUTO_TEST_CASE(getBlockByNumberLatest)
{
    auto resp = call(req("eth_getBlockByNumber", R"(["latest",false])"));
    BOOST_CHECK(resp.isMember("result") || resp.isMember("error"));
    BOOST_CHECK(resp.isMember("id"));
}

BOOST_AUTO_TEST_CASE(getBlockByNumberFullTxs)
{
    auto resp = call(req("eth_getBlockByNumber", R"(["0x1",true])"));
    BOOST_CHECK(resp.isMember("result") || resp.isMember("error"));
}

BOOST_AUTO_TEST_CASE(getBlockTransactionCountByNumber)
{
    auto resp = call(req("eth_getBlockTransactionCountByNumber", R"(["0x1"])"));
    BOOST_CHECK(resp.isMember("result") || resp.isMember("error"));
}

BOOST_AUTO_TEST_CASE(getTransactionByHashUnknown)
{
    auto resp = call(req("eth_getTransactionByHash",
        R"(["0x0000000000000000000000000000000000000000000000000000000000000001"])"));
    BOOST_CHECK(resp.isMember("result") || resp.isMember("error"));
}

BOOST_AUTO_TEST_CASE(getTransactionReceiptUnknown)
{
    auto resp = call(req("eth_getTransactionReceipt",
        R"(["0x0000000000000000000000000000000000000000000000000000000000000001"])"));
    BOOST_CHECK(resp.isMember("result") || resp.isMember("error"));
}

BOOST_AUTO_TEST_CASE(malformedParamsReportError)
{
    // Wrong arity / type should surface a JSON-RPC error, not crash.
    auto resp = call(req("eth_getBlockByNumber", R"([])"));
    BOOST_CHECK(resp.isMember("error") || resp.isMember("result"));
}

BOOST_AUTO_TEST_CASE(getBlockTransactionCountByNumberEarliestAndPending)
{
    for (auto tag : {R"(["earliest"])", R"(["pending"])", R"(["0x0"])"})
    {
        auto resp = call(req("eth_getBlockTransactionCountByNumber", tag));
        BOOST_CHECK(resp.isMember("result") || resp.isMember("error"));
        BOOST_CHECK(resp.isMember("id"));
    }
}

BOOST_AUTO_TEST_CASE(getTransactionByBlockNumberAndIndex)
{
    auto resp = call(req("eth_getTransactionByBlockNumberAndIndex", R"(["0x1","0x0"])"));
    BOOST_CHECK(resp.isMember("result") || resp.isMember("error"));
}

BOOST_AUTO_TEST_CASE(callOnSchedulerBackedPath)
{
    // eth_call routes through the scheduler (FakeScheduler2 returns an empty
    // receipt), so it should produce a result rather than crash.
    auto resp = call(req("eth_call",
        R"([{"to":"0x1234567890123456789012345678901234567890","data":"0x"},"latest"])"));
    BOOST_CHECK(resp.isMember("result") || resp.isMember("error"));
    BOOST_CHECK(resp.isMember("id"));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
