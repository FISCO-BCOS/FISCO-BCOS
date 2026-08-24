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

BOOST_AUTO_TEST_CASE(payloadAttributesMalformedQuantitiesMapToInvalidParams)
{
    // Malformed / overflowing gasLimit, minBaseFee and non-hex eip1559Params must
    // surface as InvalidParams (a plain std::exception from the converters would be
    // mapped to InternalError by the RPC entry point).
    auto expectInvalidParams = [](Json::Value attrs) {
        BOOST_CHECK_EXCEPTION(
            parsePayloadAttributes(makeAttributesParams(std::move(attrs)), engine::ApiVersion::V3),
            JsonRpcException,
            [](JsonRpcException const& error) { return error.code() == InvalidParams; });
    };

    auto malformedGasLimit = makeBaseAttributes();
    malformedGasLimit["gasLimit"] = "0xzz";
    expectInvalidParams(malformedGasLimit);

    auto overflowGasLimit = makeBaseAttributes();
    overflowGasLimit["gasLimit"] = "0x10000000000000000";  // 2^64, exceeds uint64
    expectInvalidParams(overflowGasLimit);

    auto malformedMinBaseFee = makeBaseAttributes();
    malformedMinBaseFee["minBaseFee"] = "not-a-quantity";
    expectInvalidParams(malformedMinBaseFee);

    auto overflowMinBaseFee = makeBaseAttributes();
    overflowMinBaseFee["minBaseFee"] = "0x10000000000000000";
    expectInvalidParams(overflowMinBaseFee);

    auto malformedEip1559 = makeBaseAttributes();
    malformedEip1559["eip1559Params"] = "0xzzzzzzzzzzzzzzzz";  // 8 bytes long, invalid hex
    expectInvalidParams(malformedEip1559);
}

BOOST_AUTO_TEST_CASE(payloadAttributesMinBaseFeeAcceptsBareJsonNumber)
{
    // op-node v1.19.3 serializes PayloadAttributes.MinBaseFee as a plain *uint64 with
    // no hexutil wrapper (op-service/eth/types.go:523), so the wire form is a bare JSON
    // number — every post-Jovian FCU carries e.g. "minBaseFee": 0.
    auto parseWithMinBaseFee = [](Json::Value minBaseFee) {
        auto attrs = makeBaseAttributes();
        attrs["minBaseFee"] = std::move(minBaseFee);
        return parsePayloadAttributes(
            makeAttributesParams(std::move(attrs)), engine::ApiVersion::V3);
    };

    auto zero = parseWithMinBaseFee(Json::Value(0));
    BOOST_REQUIRE(zero.has_value() && zero->minBaseFee.has_value());
    BOOST_CHECK_EQUAL(*zero->minBaseFee, 0ULL);

    // Decimal 10 must stay 10. The trap: jsoncpp asString() stringifies 10 to "10",
    // which the hex reader would parse as 0x10 = 16 — a silently forged value.
    auto ten = parseWithMinBaseFee(Json::Value(10));
    BOOST_REQUIRE(ten.has_value() && ten->minBaseFee.has_value());
    BOOST_CHECK_EQUAL(*ten->minBaseFee, 10ULL);

    auto max = parseWithMinBaseFee(Json::Value(Json::UInt64(UINT64_MAX)));
    BOOST_REQUIRE(max.has_value() && max->minBaseFee.has_value());
    BOOST_CHECK_EQUAL(*max->minBaseFee, UINT64_MAX);

    // The hex string form stays accepted: "0xa" is 10.
    auto hexTen = parseWithMinBaseFee(Json::Value("0xa"));
    BOOST_REQUIRE(hexTen.has_value() && hexTen->minBaseFee.has_value());
    BOOST_CHECK_EQUAL(*hexTen->minBaseFee, 10ULL);
}

BOOST_AUTO_TEST_CASE(payloadAttributesMinBaseFeeRejectsNonIntegerForms)
{
    auto expectInvalidParams = [](Json::Value minBaseFee) {
        auto attrs = makeBaseAttributes();
        attrs["minBaseFee"] = std::move(minBaseFee);
        BOOST_CHECK_EXCEPTION(
            parsePayloadAttributes(makeAttributesParams(std::move(attrs)), engine::ApiVersion::V3),
            JsonRpcException,
            [](JsonRpcException const& error) { return error.code() == InvalidParams; });
    };

    expectInvalidParams(Json::Value(-1));
    expectInvalidParams(Json::Value(1.5));
    expectInvalidParams(Json::Value("notahex"));
    expectInvalidParams(Json::Value(Json::arrayValue));
    expectInvalidParams(Json::Value(Json::objectValue));
}

BOOST_AUTO_TEST_CASE(payloadAttributesBareNumberStaysMinBaseFeeOnly)
{
    // The bare-number acceptance is scoped to minBaseFee: op-node wraps every other
    // uint64 attribute (timestamp, gasLimit) in hexutil.Uint64, so a bare number there
    // is still a malformed request.
    auto expectInvalidParams = [](Json::Value attrs) {
        BOOST_CHECK_EXCEPTION(
            parsePayloadAttributes(makeAttributesParams(std::move(attrs)), engine::ApiVersion::V3),
            JsonRpcException,
            [](JsonRpcException const& error) { return error.code() == InvalidParams; });
    };

    auto bareGasLimit = makeBaseAttributes();
    bareGasLimit["gasLimit"] = 30000000;
    expectInvalidParams(bareGasLimit);

    auto bareTimestamp = makeBaseAttributes();
    bareTimestamp["timestamp"] = 100;
    expectInvalidParams(bareTimestamp);
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

    auto const beaconRootHex =
        std::string("0x8888888888888888888888888888888888888888888888888888888888888888");
    auto appendV4Tail = [&](Json::Value& params) {
        // engine_newPayloadV4 takes all four params (B4): blob hashes, beacon root,
        // executionRequests.
        params.append(Json::Value(Json::arrayValue));
        params.append(beaconRootHex);
        params.append(Json::Value(Json::arrayValue));
    };

    auto ep = makeBaseExecutionPayload();
    ep["withdrawalsRoot"] = withdrawalsRootHex;
    // An ExecutionPayloadV4 carries the V2/V3 fields too, all of them required
    // (op-geth NewPayloadV4 answers -32602 for any nil among them).
    ep["withdrawals"] = Json::Value(Json::arrayValue);
    ep["blobGasUsed"] = "0x0";
    ep["excessBlobGas"] = "0x0";
    Json::Value params(Json::arrayValue);
    params.append(ep);
    appendV4Tail(params);

    // withdrawalsRoot is a V4+ (Isthmus) payload field, so the round trip runs at V4.
    auto request = parseNewPayloadRequest(params, engine::ApiVersion::V4);
    BOOST_REQUIRE(request.executionPayload.withdrawalsRoot.has_value());
    BOOST_CHECK_EQUAL(request.executionPayload.withdrawalsRoot->hexPrefixed(), withdrawalsRootHex);
    BOOST_REQUIRE(request.executionRequests.has_value());
    BOOST_CHECK(request.executionRequests->empty());

    auto serialized = serializeExecutionPayload(request.executionPayload, engine::ApiVersion::V4);
    BOOST_REQUIRE(serialized.isMember("withdrawalsRoot"));
    BOOST_CHECK_EQUAL(serialized["withdrawalsRoot"].asString(), withdrawalsRootHex);

    // Parse the serialized payload again: the field survives a full round trip.
    Json::Value reparseParams(Json::arrayValue);
    reparseParams.append(serialized);
    appendV4Tail(reparseParams);
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
