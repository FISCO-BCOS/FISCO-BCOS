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
 * @file EngineTimestampBoundaryTest.cpp
 * @brief Engine RPC boundary second/millisecond conversion (K0): op-node speaks Unix
 *        seconds, FISCO block headers store milliseconds. parse* multiplies by 1000,
 *        serializeExecutionPayload divides by 1000; internals stay in milliseconds.
 */

#include <bcos-rpc/web3jsonrpc/utils/EngineHelper.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::rpc;

namespace bcos::test
{
namespace
{
constexpr const char* h256Hex =
    "0x1111111111111111111111111111111111111111111111111111111111111111";
constexpr const char* addressHex = "0x5555555555555555555555555555555555555555";

Json::Value makeAttributesParams(std::string timestampQuantity)
{
    Json::Value params(Json::arrayValue);
    Json::Value fc(Json::objectValue);  // params[0], unused by parsePayloadAttributes
    params.append(fc);
    Json::Value attrs;
    attrs["timestamp"] = std::move(timestampQuantity);
    attrs["prevRandao"] = h256Hex;
    attrs["suggestedFeeRecipient"] = addressHex;
    params.append(attrs);
    return params;
}

Json::Value makeNewPayloadParams(std::string timestampQuantity)
{
    Json::Value params(Json::arrayValue);
    Json::Value ep;
    ep["parentHash"] = h256Hex;
    ep["stateRoot"] = h256Hex;
    ep["receiptsRoot"] = h256Hex;
    ep["prevRandao"] = h256Hex;
    ep["gasLimit"] = "0x1c9c380";
    ep["gasUsed"] = "0x0";
    ep["baseFeePerGas"] = "0x7";
    ep["blockHash"] = h256Hex;
    ep["feeRecipient"] = addressHex;
    ep["timestamp"] = std::move(timestampQuantity);
    ep["blockNumber"] = "0x1";
    params.append(ep);
    return params;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(EngineTimestampBoundaryTest)

BOOST_AUTO_TEST_CASE(payloadAttributesSecondsBecomeMillis)
{
    // op-node sends 0x64 = 100 seconds; internal PayloadAttributes carries milliseconds.
    auto attrs = parsePayloadAttributes(makeAttributesParams("0x64"), engine::ApiVersion::V3);
    BOOST_REQUIRE(attrs.has_value());
    BOOST_CHECK_EQUAL(attrs->timestamp, 100'000ULL);
}

BOOST_AUTO_TEST_CASE(newPayloadSecondsBecomeMillis)
{
    auto request = parseNewPayloadRequest(makeNewPayloadParams("0x64"), engine::ApiVersion::V1);
    BOOST_CHECK_EQUAL(request.executionPayload.timestamp, 100'000ULL);
}

BOOST_AUTO_TEST_CASE(serializedPayloadMillisBecomeSeconds)
{
    engine::ExecutionPayload payload;
    payload.timestamp = 100'000;  // milliseconds internally
    auto ep = serializeExecutionPayload(payload, engine::ApiVersion::V1);
    BOOST_CHECK_EQUAL(ep["timestamp"].asString(), "0x64");

    // Sub-second remainders truncate toward the Engine-API second.
    payload.timestamp = 100'999;
    ep = serializeExecutionPayload(payload, engine::ApiVersion::V1);
    BOOST_CHECK_EQUAL(ep["timestamp"].asString(), "0x64");
}

BOOST_AUTO_TEST_CASE(roundTripPreservesEngineSeconds)
{
    auto request =
        parseNewPayloadRequest(makeNewPayloadParams("0x68b2cf40"), engine::ApiVersion::V1);
    auto ep = serializeExecutionPayload(request.executionPayload, engine::ApiVersion::V1);
    BOOST_CHECK_EQUAL(ep["timestamp"].asString(), "0x68b2cf40");
}

BOOST_AUTO_TEST_CASE(overflowingSecondsRejected)
{
    // 2^64-1 seconds cannot be represented in milliseconds; the boundary must refuse it
    // instead of silently wrapping into a bogus header timestamp.
    BOOST_CHECK_THROW(
        parsePayloadAttributes(makeAttributesParams("0xffffffffffffffff"), engine::ApiVersion::V3),
        JsonRpcException);
    BOOST_CHECK_THROW(
        parseNewPayloadRequest(makeNewPayloadParams("0xffffffffffffffff"), engine::ApiVersion::V1),
        JsonRpcException);
}

BOOST_AUTO_TEST_CASE(int64BoundaryIsExact)
{
    // Internal header timestamps are int64_t milliseconds
    // (BlockHeader::setTimestamp(static_cast<int64_t>(...))), so the admissible
    // maximum is INT64_MAX/1000 = 9'223'372'036'854'775 seconds (0x20c49ba5e353f7).
    // One more second would convert to a value above INT64_MAX and wrap negative
    // in the header, so it must be rejected even though it still fits in uint64.
    auto attrs =
        parsePayloadAttributes(makeAttributesParams("0x20c49ba5e353f7"), engine::ApiVersion::V3);
    BOOST_REQUIRE(attrs.has_value());
    BOOST_CHECK_EQUAL(attrs->timestamp, 9'223'372'036'854'775'000ULL);

    BOOST_CHECK_THROW(
        parsePayloadAttributes(makeAttributesParams("0x20c49ba5e353f8"), engine::ApiVersion::V3),
        JsonRpcException);
    BOOST_CHECK_THROW(
        parseNewPayloadRequest(makeNewPayloadParams("0x20c49ba5e353f8"), engine::ApiVersion::V1),
        JsonRpcException);

    // Serialize direction has no symmetric hazard (int64-range ms / 1000 cannot
    // overflow); pin the boundary value round-trips to the admissible maximum.
    engine::ExecutionPayload payload;
    payload.timestamp = 9'223'372'036'854'775'000ULL;
    auto ep = serializeExecutionPayload(payload, engine::ApiVersion::V1);
    BOOST_CHECK_EQUAL(ep["timestamp"].asString(), "0x20c49ba5e353f7");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
