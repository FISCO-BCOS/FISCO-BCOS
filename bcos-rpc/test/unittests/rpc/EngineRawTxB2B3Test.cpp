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
 * @file EngineRawTxB2B3Test.cpp
 * @brief Engine raw transaction carrier (B2) and type dispatch / deposit decode (B3)
 */

#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-framework/engine/RawTransactionDispatch.h>
#include <bcos-rpc/web3jsonrpc/model/DepositTransaction.h>
#include <bcos-rpc/web3jsonrpc/utils/EngineHelper.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <string>

using namespace bcos;
using namespace bcos::rpc;
using namespace bcos::engine;

namespace
{
Json::Value makeExecutionPayloadJson(std::vector<std::string> const& transactionsHex)
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
    Json::Value transactions(Json::arrayValue);
    for (auto const& hex : transactionsHex)
    {
        transactions.append(hex);
    }
    ep["transactions"] = std::move(transactions);
    return ep;
}

struct DepositFixtureFields
{
    crypto::HashType sourceHash{"1111111111111111111111111111111111111111111111111111111111111111"};
    Address from{"2222222222222222222222222222222222222222"};
    std::optional<Address> to{Address{"3333333333333333333333333333333333333333"}};
    std::optional<u256> mint{u256(1000)};
    u256 value{7};
    uint64_t gas{21000};
    bool isSystemTx{false};
    bytes input{0xde, 0xad, 0xbe, 0xef};
};

/// Encode `0x7e || rlp([sourceHash, from, to, mint, value, gas, isSystemTx, data])`.
bytes buildDepositRaw(DepositFixtureFields const& fields)
{
    bytes body;
    codec::rlp::encode(body, fields.sourceHash);
    codec::rlp::encode(body, fields.from);
    if (fields.to.has_value())
    {
        codec::rlp::encode(body, *fields.to);
    }
    else
    {
        body.push_back(codec::rlp::BYTES_HEAD_BASE);
    }
    if (fields.mint.has_value())
    {
        codec::rlp::encode(body, *fields.mint);
    }
    else
    {
        body.push_back(codec::rlp::BYTES_HEAD_BASE);
    }
    codec::rlp::encode(body, fields.value);
    codec::rlp::encode(body, fields.gas);
    codec::rlp::encode(body, static_cast<uint64_t>(fields.isSystemTx));
    codec::rlp::encode(body, fields.input);

    bytes raw;
    raw.push_back(c_depositTxType);
    codec::rlp::encodeHeader(raw, codec::rlp::Header{.isList = true, .payloadLength = body.size()});
    raw.insert(raw.end(), body.begin(), body.end());
    return raw;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(EngineRawTxB2B3Test)

BOOST_AUTO_TEST_CASE(dispatchTableIsAuthoritative)
{
    auto kindOf = [](bytes raw) { return dispatchRawTransaction(bcos::ref(raw)); };
    BOOST_CHECK(kindOf({0x01, 0xaa}) == RawTransactionKind::AccessList);
    BOOST_CHECK(kindOf({0x02, 0xaa}) == RawTransactionKind::DynamicFee);
    BOOST_CHECK(kindOf({0x04, 0xaa}) == RawTransactionKind::SetCode);
    BOOST_CHECK(kindOf({0x7e, 0xaa}) == RawTransactionKind::Deposit);
    // RLP list header range => legacy.
    BOOST_CHECK(kindOf({0xc0}) == RawTransactionKind::Legacy);
    BOOST_CHECK(kindOf({0xf8, 0x01}) == RawTransactionKind::Legacy);
    BOOST_CHECK(kindOf({0xff}) == RawTransactionKind::Legacy);
    // Blob: parseable as a category, but never admissible.
    BOOST_CHECK(kindOf({0x03, 0xaa}) == RawTransactionKind::Blob);
    BOOST_CHECK(!isRawTransactionPayloadAdmissible(RawTransactionKind::Blob));
    // 0x00 is not a valid EIP-2718 type; reserved/unknown bytes are unsupported.
    BOOST_CHECK(kindOf({0x00, 0xaa}) == RawTransactionKind::Unsupported);
    BOOST_CHECK(kindOf({0x05, 0xaa}) == RawTransactionKind::Unsupported);
    BOOST_CHECK(kindOf({0x7f, 0xaa}) == RawTransactionKind::Unsupported);
    BOOST_CHECK(kindOf({0xbf, 0xaa}) == RawTransactionKind::Unsupported);
    BOOST_CHECK(kindOf({}) == RawTransactionKind::Unsupported);
    BOOST_CHECK(!isRawTransactionPayloadAdmissible(RawTransactionKind::Unsupported));
    BOOST_CHECK(isRawTransactionPayloadAdmissible(RawTransactionKind::Legacy));
    BOOST_CHECK(isRawTransactionPayloadAdmissible(RawTransactionKind::Deposit));
}

BOOST_AUTO_TEST_CASE(rawTransactionBytesSurviveParseSerializeRoundTrip)
{
    // dep-1 as raw hex plus a legacy and a typed transaction: the parser does hex->bytes
    // only, and serialization returns the exact same hex — the three-point byte equality
    // the Engine codec must guarantee (attributes leg asserted below).
    auto const dep1Hex = toHexStringWithPrefix(buildDepositRaw(DepositFixtureFields{}));
    auto const legacyHex = std::string("0xc9808080808080808080");
    auto const typedHex = std::string("0x02f8010203");

    Json::Value params(Json::arrayValue);
    params.append(makeExecutionPayloadJson({dep1Hex, legacyHex, typedHex}));

    auto request = parseNewPayloadRequest(params, engine::ApiVersion::V1);
    BOOST_REQUIRE_EQUAL(request.executionPayload.transactions.size(), 3);
    BOOST_CHECK_EQUAL(toHexStringWithPrefix(request.executionPayload.transactions[0].raw), dep1Hex);
    BOOST_CHECK_EQUAL(
        toHexStringWithPrefix(request.executionPayload.transactions[1].raw), legacyHex);
    BOOST_CHECK_EQUAL(
        toHexStringWithPrefix(request.executionPayload.transactions[2].raw), typedHex);
    // No decoding at the parse boundary.
    BOOST_CHECK(request.executionPayload.transactions[0].decoded == nullptr);

    auto serialized = serializeExecutionPayload(request.executionPayload, engine::ApiVersion::V1);
    BOOST_REQUIRE_EQUAL(serialized["transactions"].size(), 3);
    BOOST_CHECK_EQUAL(serialized["transactions"][0U].asString(), dep1Hex);
    BOOST_CHECK_EQUAL(serialized["transactions"][1U].asString(), legacyHex);
    BOOST_CHECK_EQUAL(serialized["transactions"][2U].asString(), typedHex);

    // Attributes leg: the forced-transaction list keeps dep-1 verbatim as well.
    Json::Value attrsParams(Json::arrayValue);
    Json::Value forkchoice(Json::objectValue);
    forkchoice["headBlockHash"] =
        "0x3333333333333333333333333333333333333333333333333333333333333333";
    forkchoice["safeBlockHash"] =
        "0x3333333333333333333333333333333333333333333333333333333333333333";
    forkchoice["finalizedBlockHash"] =
        "0x3333333333333333333333333333333333333333333333333333333333333333";
    attrsParams.append(std::move(forkchoice));
    Json::Value attrs(Json::objectValue);
    attrs["timestamp"] = "0x64";
    attrs["prevRandao"] = "0x1111111111111111111111111111111111111111111111111111111111111111";
    attrs["suggestedFeeRecipient"] = "0x2222222222222222222222222222222222222222";
    Json::Value attrsTxs(Json::arrayValue);
    attrsTxs.append(dep1Hex);
    attrs["transactions"] = std::move(attrsTxs);
    attrsParams.append(std::move(attrs));
    auto parsedAttrs = parsePayloadAttributes(attrsParams, engine::ApiVersion::V3);
    BOOST_REQUIRE(parsedAttrs.has_value());
    BOOST_REQUIRE(parsedAttrs->transactions.has_value());
    BOOST_CHECK_EQUAL(parsedAttrs->transactions->at(0), dep1Hex);
}

BOOST_AUTO_TEST_CASE(parseNewPayloadRejectsNonHexTransactionEntries)
{
    // Invalid hex characters.
    Json::Value params(Json::arrayValue);
    params.append(makeExecutionPayloadJson({"0xzz"}));
    BOOST_CHECK_THROW(parseNewPayloadRequest(params, engine::ApiVersion::V1), JsonRpcException);

    // Missing 0x prefix.
    Json::Value noPrefixParams(Json::arrayValue);
    noPrefixParams.append(makeExecutionPayloadJson({"7e01020304"}));
    BOOST_CHECK_THROW(
        parseNewPayloadRequest(noPrefixParams, engine::ApiVersion::V1), JsonRpcException);

    // Non-string element.
    auto nonStringPayload = makeExecutionPayloadJson({});
    nonStringPayload["transactions"].append(Json::Value(123));
    Json::Value nonStringParams(Json::arrayValue);
    nonStringParams.append(nonStringPayload);
    BOOST_CHECK_THROW(
        parseNewPayloadRequest(nonStringParams, engine::ApiVersion::V1), JsonRpcException);

    // Odd-length hex (fromHex would silently left-pad a '0' nibble — must reject to
    // keep the byte-fidelity contract).
    Json::Value oddParams(Json::arrayValue);
    oddParams.append(makeExecutionPayloadJson({"0x123"}));
    BOOST_CHECK_THROW(parseNewPayloadRequest(oddParams, engine::ApiVersion::V1), JsonRpcException);

    // Bare "0x" (empty transaction).
    Json::Value emptyParams(Json::arrayValue);
    emptyParams.append(makeExecutionPayloadJson({"0x"}));
    BOOST_CHECK_THROW(
        parseNewPayloadRequest(emptyParams, engine::ApiVersion::V1), JsonRpcException);

    // transactions must be an array.
    auto scalarPayload = makeExecutionPayloadJson({});
    scalarPayload["transactions"] = "0x7e01020304";
    Json::Value scalarParams(Json::arrayValue);
    scalarParams.append(scalarPayload);
    BOOST_CHECK_THROW(
        parseNewPayloadRequest(scalarParams, engine::ApiVersion::V1), JsonRpcException);
}

BOOST_AUTO_TEST_CASE(depositDecodeFullFields)
{
    DepositFixtureFields fields;
    fields.isSystemTx = true;
    auto raw = buildDepositRaw(fields);
    auto cursor = bcos::ref(raw);

    DepositTransaction deposit;
    auto error = decodeDepositTransaction(cursor, deposit);
    BOOST_REQUIRE(error == nullptr);
    BOOST_CHECK_EQUAL(deposit.sourceHash, fields.sourceHash);
    BOOST_CHECK_EQUAL(deposit.from, fields.from);
    BOOST_REQUIRE(deposit.to.has_value());
    BOOST_CHECK_EQUAL(*deposit.to, *fields.to);
    BOOST_REQUIRE(deposit.mint.has_value());
    BOOST_CHECK_EQUAL(*deposit.mint, *fields.mint);
    BOOST_CHECK_EQUAL(deposit.value, fields.value);
    BOOST_CHECK_EQUAL(deposit.gas, fields.gas);
    BOOST_CHECK_EQUAL(deposit.isSystemTx, true);
    BOOST_CHECK(deposit.input == fields.input);
    BOOST_CHECK(cursor.empty());
}

BOOST_AUTO_TEST_CASE(depositDecodeCreationAndNilMint)
{
    DepositFixtureFields fields;
    fields.to = std::nullopt;    // contract creation: empty RLP item
    fields.mint = std::nullopt;  // nil mint: empty RLP item (op-geth *big.Int nil)
    auto raw = buildDepositRaw(fields);
    auto cursor = bcos::ref(raw);

    DepositTransaction deposit;
    auto error = decodeDepositTransaction(cursor, deposit);
    BOOST_REQUIRE(error == nullptr);
    BOOST_CHECK(!deposit.to.has_value());
    BOOST_CHECK(!deposit.mint.has_value());
}

BOOST_AUTO_TEST_CASE(depositDecodeRejectsMalformedInput)
{
    // Wrong type byte.
    bytes notDeposit{0x02, 0xc0};
    auto cursor = bcos::ref(notDeposit);
    DepositTransaction deposit;
    BOOST_CHECK(decodeDepositTransaction(cursor, deposit) != nullptr);

    // Truncated body.
    auto raw = buildDepositRaw(DepositFixtureFields{});
    raw.resize(raw.size() - 3);
    auto truncatedCursor = bcos::ref(raw);
    BOOST_CHECK(decodeDepositTransaction(truncatedCursor, deposit) != nullptr);

    // Trailing bytes inside the list.
    auto goodRaw = buildDepositRaw(DepositFixtureFields{});
    bytes body(goodRaw.begin() + 1 + 2, goodRaw.end());  // strip type byte + list header
    body.push_back(0x01);                                // extra trailing item
    bytes tampered;
    tampered.push_back(c_depositTxType);
    codec::rlp::encodeHeader(
        tampered, codec::rlp::Header{.isList = true, .payloadLength = body.size()});
    tampered.insert(tampered.end(), body.begin(), body.end());
    auto tamperedCursor = bcos::ref(tampered);
    BOOST_CHECK(decodeDepositTransaction(tamperedCursor, deposit) != nullptr);
}

BOOST_AUTO_TEST_CASE(depositJsonMatchesGethShape)
{
    DepositFixtureFields fields;
    auto raw = buildDepositRaw(fields);
    auto cursor = bcos::ref(raw);
    DepositTransaction deposit;
    BOOST_REQUIRE(decodeDepositTransaction(cursor, deposit) == nullptr);

    Json::Value result(Json::objectValue);
    combineDepositTxResponse(result, deposit);

    BOOST_CHECK_EQUAL(result["type"].asString(), "0x7e");
    BOOST_CHECK_EQUAL(result["sourceHash"].asString(), fields.sourceHash.hexPrefixed());
    BOOST_CHECK_EQUAL(result["gas"].asString(), "0x5208");
    BOOST_CHECK_EQUAL(result["value"].asString(), "0x7");
    BOOST_CHECK_EQUAL(result["input"].asString(), "0xdeadbeef");
    BOOST_CHECK_EQUAL(result["mint"].asString(), "0x3e8");
    // op-geth omits isSystemTx when false and emits it only when true.
    BOOST_CHECK(!result.isMember("isSystemTx"));
    // Deposits are unsigned and carry no nonce of their own (the deposit nonce lives in
    // the receipt); op-geth emits zero quantities.
    BOOST_CHECK_EQUAL(result["nonce"].asString(), "0x0");
    BOOST_CHECK_EQUAL(result["gasPrice"].asString(), "0x0");
    BOOST_CHECK_EQUAL(result["v"].asString(), "0x0");
    BOOST_CHECK_EQUAL(result["r"].asString(), "0x0");
    BOOST_CHECK_EQUAL(result["s"].asString(), "0x0");
    BOOST_CHECK(!result.isMember("chainId"));

    // isSystemTx=true is emitted; a creation deposit reports "to": null.
    DepositFixtureFields systemFields;
    systemFields.isSystemTx = true;
    systemFields.to = std::nullopt;
    systemFields.mint = std::nullopt;
    auto systemRaw = buildDepositRaw(systemFields);
    auto systemCursor = bcos::ref(systemRaw);
    DepositTransaction systemDeposit;
    BOOST_REQUIRE(decodeDepositTransaction(systemCursor, systemDeposit) == nullptr);
    Json::Value systemResult(Json::objectValue);
    combineDepositTxResponse(systemResult, systemDeposit);
    BOOST_CHECK_EQUAL(systemResult["isSystemTx"].asBool(), true);
    BOOST_CHECK(systemResult["to"].isNull());
    BOOST_CHECK(!systemResult.isMember("mint"));
}

BOOST_AUTO_TEST_SUITE_END()
