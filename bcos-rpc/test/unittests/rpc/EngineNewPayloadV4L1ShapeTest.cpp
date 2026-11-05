/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
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
 * @brief engine_newPayloadV4 parses the L1 payload shape: ExecutionPayloadV3 plus
 *        executionRequests, with no OP-dialect withdrawalsRoot (execution-apis prague.md).
 *        Before this test the parser answered -32602 for every L1 Prague/Osaka payload.
 * @file EngineNewPayloadV4L1ShapeTest.cpp
 */
#include <bcos-rpc/web3jsonrpc/utils/EngineHelper.h>
#include <boost/test/unit_test.hpp>
#include <string>

using namespace bcos;
using namespace bcos::rpc;

namespace
{
Json::Value makeL1V4Params()
{
    Json::Value ep(Json::objectValue);
    ep["parentHash"] = "0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    ep["feeRecipient"] = "0xbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    ep["stateRoot"] = "0xcccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
    ep["receiptsRoot"] = "0xdddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";
    ep["logsBloom"] = "0x" + std::string(512, '0');
    ep["prevRandao"] = "0xeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";
    ep["blockNumber"] = "0x1";
    ep["gasLimit"] = "0x5208";
    ep["gasUsed"] = "0x0";
    ep["timestamp"] = "0x1";
    ep["extraData"] = "0x";
    ep["baseFeePerGas"] = "0x1";
    ep["blockHash"] = "0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";
    ep["transactions"] = Json::Value(Json::arrayValue);
    ep["withdrawals"] = Json::Value(Json::arrayValue);
    ep["blobGasUsed"] = "0x0";
    ep["excessBlobGas"] = "0x0";
    Json::Value params(Json::arrayValue);
    params.append(ep);
    params.append(Json::Value(Json::arrayValue));  // expectedBlobVersionedHashes
    params.append("0x8888888888888888888888888888888888888888888888888888888888888888");
    params.append(Json::Value(Json::arrayValue));  // executionRequests
    return params;
}

int errorCodeOf(Json::Value const& params)
{
    try
    {
        (void)parseNewPayloadRequest(params, engine::ApiVersion::V4);
    }
    catch (JsonRpcException const& e)
    {
        return e.code();
    }
    return 0;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(EngineNewPayloadV4L1ShapeTest)

BOOST_AUTO_TEST_CASE(l1PayloadWithoutWithdrawalsRootParses)
{
    auto request = parseNewPayloadRequest(makeL1V4Params(), engine::ApiVersion::V4);
    BOOST_CHECK(!request.executionPayload.withdrawalsRoot.has_value());
    BOOST_REQUIRE(request.executionPayload.blobGasUsed.has_value());
    BOOST_REQUIRE(request.parentBeaconBlockRoot.has_value());
    BOOST_REQUIRE(request.executionRequests.has_value());
}

BOOST_AUTO_TEST_CASE(nullWithdrawalsRootReadsAsAbsent)
{
    auto params = makeL1V4Params();
    params[0u]["withdrawalsRoot"] = Json::Value(Json::nullValue);
    auto request = parseNewPayloadRequest(params, engine::ApiVersion::V4);
    BOOST_CHECK(!request.executionPayload.withdrawalsRoot.has_value());
}

BOOST_AUTO_TEST_CASE(presentWithdrawalsRootIsParsed)
{
    auto params = makeL1V4Params();
    params[0u]["withdrawalsRoot"] =
        "0x9999999999999999999999999999999999999999999999999999999999999999";
    auto request = parseNewPayloadRequest(params, engine::ApiVersion::V4);
    BOOST_REQUIRE(request.executionPayload.withdrawalsRoot.has_value());
    BOOST_CHECK_EQUAL(request.executionPayload.withdrawalsRoot->hexPrefixed(),
        "0x9999999999999999999999999999999999999999999999999999999999999999");
}

BOOST_AUTO_TEST_CASE(wronglyTypedWithdrawalsRootIsInvalidParams)
{
    auto params = makeL1V4Params();
    params[0u]["withdrawalsRoot"] = 7;
    BOOST_CHECK_EQUAL(errorCodeOf(params), static_cast<int>(InvalidParams));
}

BOOST_AUTO_TEST_CASE(executionRequestsShapeIsInvalidParams)
{
    // execution-apis prague.md: request_type || non-empty request_data, sorted by type,
    // ascending, no duplicates. Each violation is -32602, never a payload status.
    auto withRequests = [](std::initializer_list<char const*> items) {
        auto params = makeL1V4Params();
        Json::Value list(Json::arrayValue);
        for (auto const* item : items)
        {
            list.append(item);
        }
        params[3u] = list;
        return params;
    };
    auto accepted = parseNewPayloadRequest(
        withRequests({"0x00aa", "0x01bb", "0x02cc"}), engine::ApiVersion::V4);
    BOOST_REQUIRE(accepted.executionRequests.has_value());
    BOOST_CHECK_EQUAL(accepted.executionRequests->size(), 3U);

    BOOST_CHECK_EQUAL(errorCodeOf(withRequests({"0x00"})), static_cast<int>(InvalidParams));
    BOOST_CHECK_EQUAL(errorCodeOf(withRequests({"0x"})), static_cast<int>(InvalidParams));
    BOOST_CHECK_EQUAL(
        errorCodeOf(withRequests({"0x01bb", "0x00aa"})), static_cast<int>(InvalidParams));
    BOOST_CHECK_EQUAL(
        errorCodeOf(withRequests({"0x00aa", "0x00bb"})), static_cast<int>(InvalidParams));
    BOOST_CHECK_EQUAL(
        errorCodeOf(withRequests({"0x00aa", "0x01bb", "0x03"})), static_cast<int>(InvalidParams));
}

BOOST_AUTO_TEST_CASE(blobGasFieldsFollowTheMethodVersion)
{
    // geth NewPayloadV3: nil blob gas field -> -32602; NewPayloadV2: non-nil -> -32602.
    auto v3 = makeL1V4Params();
    v3.resize(3);  // [payload, blobHashes, beaconRoot]
    (void)parseNewPayloadRequest(v3, engine::ApiVersion::V3);
    for (auto const* field : {"blobGasUsed", "excessBlobGas"})
    {
        auto missing = v3;
        missing[0u].removeMember(field);
        int code = 0;
        try
        {
            (void)parseNewPayloadRequest(missing, engine::ApiVersion::V3);
        }
        catch (JsonRpcException const& e)
        {
            code = e.code();
        }
        BOOST_CHECK_EQUAL(code, static_cast<int>(InvalidParams));
    }
    auto v2 = makeL1V4Params();
    v2.resize(1);
    int code = 0;
    try
    {
        (void)parseNewPayloadRequest(v2, engine::ApiVersion::V2);  // carries both blob fields
    }
    catch (JsonRpcException const& e)
    {
        code = e.code();
    }
    BOOST_CHECK_EQUAL(code, static_cast<int>(InvalidParams));
    v2[0u].removeMember("blobGasUsed");
    v2[0u].removeMember("excessBlobGas");
    auto parsed = parseNewPayloadRequest(v2, engine::ApiVersion::V2);
    BOOST_CHECK(!parsed.executionPayload.blobGasUsed.has_value());
}

BOOST_AUTO_TEST_CASE(otherV4FieldsStayRequired)
{
    for (auto const* field : {"withdrawals", "blobGasUsed", "excessBlobGas"})
    {
        auto params = makeL1V4Params();
        params[0u].removeMember(field);
        BOOST_CHECK_EQUAL(errorCodeOf(params), static_cast<int>(InvalidParams));
    }
}

BOOST_AUTO_TEST_SUITE_END()
