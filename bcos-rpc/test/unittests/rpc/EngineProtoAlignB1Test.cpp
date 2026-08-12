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
 * @file EngineProtoAlignB1Test.cpp
 * @brief Engine protocol structure alignment tests: OP payload-attributes fields,
 *        ExecutionPayload.withdrawalsRoot and GetPayloadData.parentBeaconBlockRoot.
 */

#include <bcos-rpc/web3jsonrpc/utils/EngineHelper.h>
#include <json/json.h>
#include <boost/test/unit_test.hpp>
#include <memory>
#include <string>

using namespace bcos;
using namespace bcos::rpc;

namespace
{
Json::Value makeBaseAttributes()
{
    Json::Value attrs(Json::objectValue);
    attrs["timestamp"] = "0x64";
    attrs["prevRandao"] = "0x1111111111111111111111111111111111111111111111111111111111111111";
    attrs["suggestedFeeRecipient"] = "0x2222222222222222222222222222222222222222";
    return attrs;
}

Json::Value makeAttributesParams(Json::Value attrs)
{
    Json::Value params(Json::arrayValue);
    Json::Value forkchoice(Json::objectValue);
    forkchoice["headBlockHash"] =
        "0x3333333333333333333333333333333333333333333333333333333333333333";
    forkchoice["safeBlockHash"] =
        "0x3333333333333333333333333333333333333333333333333333333333333333";
    forkchoice["finalizedBlockHash"] =
        "0x3333333333333333333333333333333333333333333333333333333333333333";
    params.append(std::move(forkchoice));
    params.append(std::move(attrs));
    return params;
}

Json::Value makeBaseExecutionPayload()
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
    ep["extraData"] = "0x1234";
    ep["baseFeePerGas"] = "0x1";
    ep["blockHash"] = "0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";
    ep["transactions"] = Json::Value(Json::arrayValue);
    return ep;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(EngineProtoAlignB1Test)

BOOST_AUTO_TEST_CASE(payloadAttributesOpFieldsParseAndSerialize)
{
    auto attrs = makeBaseAttributes();
    Json::Value transactions(Json::arrayValue);
    // Raw EIP-2718 hex strings: a deposit (0x7e) and a type-2 transaction, opaque here.
    transactions.append("0x7e01020304");
    transactions.append("0x02f8720102");
    attrs["transactions"] = transactions;
    attrs["noTxPool"] = true;
    attrs["gasLimit"] = "0x1c9c380";
    attrs["eip1559Params"] = "0x0000000800000002";
    attrs["minBaseFee"] = "0x3b9aca00";

    auto parsed = parsePayloadAttributes(makeAttributesParams(attrs), engine::ApiVersion::V3);
    BOOST_REQUIRE(parsed.has_value());
    BOOST_REQUIRE(parsed->transactions.has_value());
    BOOST_REQUIRE_EQUAL(parsed->transactions->size(), 2);
    BOOST_CHECK_EQUAL(parsed->transactions->at(0), "0x7e01020304");
    BOOST_CHECK_EQUAL(parsed->transactions->at(1), "0x02f8720102");
    BOOST_REQUIRE(parsed->noTxPool.has_value());
    BOOST_CHECK_EQUAL(*parsed->noTxPool, true);
    BOOST_REQUIRE(parsed->gasLimit.has_value());
    BOOST_CHECK_EQUAL(*parsed->gasLimit, 30000000ULL);
    BOOST_REQUIRE(parsed->eip1559Params.has_value());
    BOOST_CHECK_EQUAL(parsed->eip1559Params->size(), 8);
    BOOST_CHECK_EQUAL(toHexStringWithPrefix(*parsed->eip1559Params), "0x0000000800000002");
    BOOST_REQUIRE(parsed->minBaseFee.has_value());
    BOOST_CHECK_EQUAL(*parsed->minBaseFee, 1000000000ULL);

    // Serialization round-trip: serialize the parsed struct and parse it again.
    auto serialized = serializePayloadAttributes(*parsed);
    BOOST_CHECK_EQUAL(serialized["noTxPool"].asBool(), true);
    BOOST_CHECK_EQUAL(serialized["gasLimit"].asString(), "0x1c9c380");
    BOOST_CHECK_EQUAL(serialized["eip1559Params"].asString(), "0x0000000800000002");
    BOOST_CHECK_EQUAL(serialized["minBaseFee"].asString(), "0x3b9aca00");
    BOOST_REQUIRE_EQUAL(serialized["transactions"].size(), 2);
    BOOST_CHECK_EQUAL(serialized["transactions"][0u].asString(), "0x7e01020304");

    auto reparsed =
        parsePayloadAttributes(makeAttributesParams(serialized), engine::ApiVersion::V3);
    BOOST_REQUIRE(reparsed.has_value());
    BOOST_CHECK(reparsed->transactions == parsed->transactions);
    BOOST_CHECK(reparsed->noTxPool == parsed->noTxPool);
    BOOST_CHECK(reparsed->gasLimit == parsed->gasLimit);
    BOOST_CHECK(reparsed->eip1559Params == parsed->eip1559Params);
    BOOST_CHECK(reparsed->minBaseFee == parsed->minBaseFee);
}

BOOST_AUTO_TEST_CASE(payloadAttributesOpFieldsDefaultToAbsent)
{
    // No OP fields in the JSON: parsing keeps them unset (current behavior preserved),
    // and serialization does not invent them.
    auto parsed =
        parsePayloadAttributes(makeAttributesParams(makeBaseAttributes()), engine::ApiVersion::V3);
    BOOST_REQUIRE(parsed.has_value());
    BOOST_CHECK(!parsed->transactions.has_value());
    BOOST_CHECK(!parsed->noTxPool.has_value());
    BOOST_CHECK(!parsed->gasLimit.has_value());
    BOOST_CHECK(!parsed->eip1559Params.has_value());
    BOOST_CHECK(!parsed->minBaseFee.has_value());

    auto serialized = serializePayloadAttributes(*parsed);
    BOOST_CHECK(!serialized.isMember("transactions"));
    BOOST_CHECK(!serialized.isMember("noTxPool"));
    BOOST_CHECK(!serialized.isMember("gasLimit"));
    BOOST_CHECK(!serialized.isMember("eip1559Params"));
    BOOST_CHECK(!serialized.isMember("minBaseFee"));
}

BOOST_AUTO_TEST_CASE(payloadAttributesEip1559ParamsLengthRejected)
{
    auto attrs = makeBaseAttributes();
    attrs["eip1559Params"] = "0x00000008";  // 4 bytes, must be 8
    BOOST_CHECK_THROW(parsePayloadAttributes(makeAttributesParams(attrs), engine::ApiVersion::V3),
        JsonRpcException);
}

BOOST_AUTO_TEST_CASE(payloadAttributesTransactionsMustBeArray)
{
    auto attrs = makeBaseAttributes();
    attrs["transactions"] = "0x7e01020304";  // scalar instead of array
    BOOST_CHECK_THROW(parsePayloadAttributes(makeAttributesParams(attrs), engine::ApiVersion::V3),
        JsonRpcException);
}

BOOST_AUTO_TEST_CASE(payloadAttributesTransactionsElementsMustBePrefixedHexStrings)
{
    // Non-string element.
    auto attrs = makeBaseAttributes();
    Json::Value transactions(Json::arrayValue);
    transactions.append(Json::Value(123));
    attrs["transactions"] = transactions;
    BOOST_CHECK_THROW(parsePayloadAttributes(makeAttributesParams(attrs), engine::ApiVersion::V3),
        JsonRpcException);

    // Invalid hex characters.
    auto badHexAttrs = makeBaseAttributes();
    Json::Value badHexTxs(Json::arrayValue);
    badHexTxs.append("0xzz");
    badHexAttrs["transactions"] = badHexTxs;
    BOOST_CHECK_THROW(
        parsePayloadAttributes(makeAttributesParams(badHexAttrs), engine::ApiVersion::V3),
        JsonRpcException);

    // Missing 0x prefix.
    auto noPrefixAttrs = makeBaseAttributes();
    Json::Value noPrefixTxs(Json::arrayValue);
    noPrefixTxs.append("7e01020304");
    noPrefixAttrs["transactions"] = noPrefixTxs;
    BOOST_CHECK_THROW(
        parsePayloadAttributes(makeAttributesParams(noPrefixAttrs), engine::ApiVersion::V3),
        JsonRpcException);

    // Odd-length hex (fromHex would silently left-pad a '0' nibble — must reject).
    auto oddAttrs = makeBaseAttributes();
    Json::Value oddTxs(Json::arrayValue);
    oddTxs.append("0x123");
    oddAttrs["transactions"] = oddTxs;
    BOOST_CHECK_THROW(
        parsePayloadAttributes(makeAttributesParams(oddAttrs), engine::ApiVersion::V3),
        JsonRpcException);

    // Bare "0x" (empty transaction).
    auto emptyAttrs = makeBaseAttributes();
    Json::Value emptyTxs(Json::arrayValue);
    emptyTxs.append("0x");
    emptyAttrs["transactions"] = emptyTxs;
    BOOST_CHECK_THROW(
        parsePayloadAttributes(makeAttributesParams(emptyAttrs), engine::ApiVersion::V3),
        JsonRpcException);
}

BOOST_AUTO_TEST_CASE(payloadAttributesNoTxPoolMustBeBoolean)
{
    // jsoncpp asBool() silently converts numbers and strings; the parser must gate on
    // the JSON type instead.
    auto stringAttrs = makeBaseAttributes();
    stringAttrs["noTxPool"] = "true";
    BOOST_CHECK_THROW(
        parsePayloadAttributes(makeAttributesParams(stringAttrs), engine::ApiVersion::V3),
        JsonRpcException);

    auto numberAttrs = makeBaseAttributes();
    numberAttrs["noTxPool"] = 1;
    BOOST_CHECK_THROW(
        parsePayloadAttributes(makeAttributesParams(numberAttrs), engine::ApiVersion::V3),
        JsonRpcException);
}

BOOST_AUTO_TEST_CASE(executionPayloadWithdrawalsRootRoundTrip)
{
    auto const withdrawalsRootHex =
        std::string("0x9999999999999999999999999999999999999999999999999999999999999999");

    auto ep = makeBaseExecutionPayload();
    ep["withdrawalsRoot"] = withdrawalsRootHex;
    Json::Value params(Json::arrayValue);
    params.append(ep);

    // withdrawalsRoot is a V4+ (Isthmus) payload field, so the round trip runs at V4.
    auto request = parseNewPayloadRequest(params, engine::ApiVersion::V4);
    BOOST_REQUIRE(request.executionPayload.withdrawalsRoot.has_value());
    BOOST_CHECK_EQUAL(request.executionPayload.withdrawalsRoot->hexPrefixed(), withdrawalsRootHex);

    auto serialized = serializeExecutionPayload(request.executionPayload, engine::ApiVersion::V4);
    BOOST_REQUIRE(serialized.isMember("withdrawalsRoot"));
    BOOST_CHECK_EQUAL(serialized["withdrawalsRoot"].asString(), withdrawalsRootHex);

    // Parse the serialized payload again: the field survives a full round trip.
    Json::Value reparseParams(Json::arrayValue);
    reparseParams.append(serialized);
    auto reparsed = parseNewPayloadRequest(reparseParams, engine::ApiVersion::V4);
    BOOST_CHECK(
        reparsed.executionPayload.withdrawalsRoot == request.executionPayload.withdrawalsRoot);
}

BOOST_AUTO_TEST_CASE(executionPayloadWithdrawalsRootIgnoredBeforeV4)
{
    auto ep = makeBaseExecutionPayload();
    ep["withdrawalsRoot"] = "0x9999999999999999999999999999999999999999999999999999999999999999";
    Json::Value params(Json::arrayValue);
    params.append(ep);

    // V1-V3: the field is ignored (matches op-geth NewPayloadV3, which performs no
    // withdrawalsRoot validation; the Isthmus spec leaves pre-V4 handling unspecified).
    auto request = parseNewPayloadRequest(params, engine::ApiVersion::V3);
    BOOST_CHECK(!request.executionPayload.withdrawalsRoot.has_value());

    // Even with the struct field set, V1-V3 serialization never emits it.
    request.executionPayload.withdrawalsRoot =
        parseH256("0x9999999999999999999999999999999999999999999999999999999999999999");
    auto serialized = serializeExecutionPayload(request.executionPayload, engine::ApiVersion::V3);
    BOOST_CHECK(!serialized.isMember("withdrawalsRoot"));
}

BOOST_AUTO_TEST_CASE(executionPayloadWithdrawalsRootAbsentByDefault)
{
    Json::Value params(Json::arrayValue);
    params.append(makeBaseExecutionPayload());

    auto request = parseNewPayloadRequest(params, engine::ApiVersion::V1);
    BOOST_CHECK(!request.executionPayload.withdrawalsRoot.has_value());

    auto serialized = serializeExecutionPayload(request.executionPayload, engine::ApiVersion::V1);
    BOOST_CHECK(!serialized.isMember("withdrawalsRoot"));
}

BOOST_AUTO_TEST_CASE(getPayloadResponseCarriesParentBeaconBlockRoot)
{
    auto const beaconRootHex =
        std::string("0x8888888888888888888888888888888888888888888888888888888888888888");
    auto data = std::make_unique<engine::GetPayloadData>();
    data->parentBeaconBlockRoot = parseH256(beaconRootHex);

    Json::Value result;
    combineGetPayloadResponse(result, data, engine::ApiVersion::V3);
    BOOST_REQUIRE(result.isMember("parentBeaconBlockRoot"));
    BOOST_CHECK_EQUAL(result["parentBeaconBlockRoot"].asString(), beaconRootHex);

    // Absent beacon root: the member is not emitted (V1-V3 responses unchanged).
    auto emptyData = std::make_unique<engine::GetPayloadData>();
    Json::Value emptyResult;
    combineGetPayloadResponse(emptyResult, emptyData, engine::ApiVersion::V3);
    BOOST_CHECK(!emptyResult.isMember("parentBeaconBlockRoot"));
}

BOOST_AUTO_TEST_SUITE_END()
