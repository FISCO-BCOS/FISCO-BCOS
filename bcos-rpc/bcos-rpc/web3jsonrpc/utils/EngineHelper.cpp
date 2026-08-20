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
 * @file EngineHelper.cpp
 * @date 2026/7/16
 */

#include "EngineHelper.h"
#include <limits>

using namespace bcos;
using namespace bcos::rpc;

namespace
{
/// Holocene eip1559Params: 4-byte denominator followed by 4-byte elasticity.
constexpr std::size_t c_eip1559ParamsBytes = 8;

/// Validate and decode one element of a JSON `transactions` array. The element must be
/// a string, "0x"-prefixed and valid hex. Shared by the payloadAttributes and the
/// executionPayload parse paths so both reject malformed entries with the same
/// InvalidParams shape, naming the offending index.
bcos::bytes parseRawTransactionElement(
    Json::Value const& element, char const* container, Json::ArrayIndex index)
{
    auto reject = [&]() -> bcos::bytes {
        BOOST_THROW_EXCEPTION(bcos::rpc::JsonRpcException(bcos::rpc::InvalidParams,
            std::string(container) + ".transactions[" + std::to_string(index) +
                "] must be a 0x-prefixed hex string"));
    };
    if (!element.isString())
    {
        return reject();
    }
    auto const hex = element.asString();
    // <= 2 also rejects a bare "0x" (empty transaction); the odd-length check matters
    // because bcos::fromHex silently left-pads odd-length input with a '0' nibble,
    // which would break the "hex-decoded verbatim" byte-fidelity contract.
    if (hex.size() <= 2 || hex.size() % 2 != 0 || hex[0] != '0' || (hex[1] != 'x' && hex[1] != 'X'))
    {
        return reject();
    }
    try
    {
        return bcos::fromHex(hex);
    }
    catch (bcos::BadHexCharacter const&)
    {
        return reject();
    }
}

/// Strict read of a u256-typed hex quantity out of a JSON object, reported the way
/// parseH256 reports a malformed hash. `bcos::fromBigQuantity` cannot be used directly on
/// wire input: it delegates to hex2u, which catches every parse failure and returns 0, so
/// `blobGasUsed: "0xnothex"` would be accepted as the value 0 rather than answered with
/// -32602 — a silently forged value on a field the CL believes it set.
///
/// The isString() gate is what makes the check complete. jsoncpp's asString() does NOT
/// reject a number or a bool: it stringifies them (json_value.cpp Value::asString), so
/// `"gasLimit": 10` used to arrive as "10" and then be read as HEX 0x10 = 16 — a value
/// forgery worse than the malformed-hex case. Only an array/object throws
/// Json::LogicError there, which would surface as -32603.
bcos::u256 parseBigQuantity(Json::Value const& value, std::string_view field)
{
    if (value.isString())
    {
        if (auto parsed = bcos::safeFromBigQuantity(value.asString()))
        {
            return *parsed;
        }
    }
    BOOST_THROW_EXCEPTION(bcos::rpc::JsonRpcException(bcos::rpc::InvalidParams,
        std::string(field) + " must be a hex quantity string of at most 32 bytes"));
}

/// The uint64-typed counterpart, over the strict bcos::safeFromQuantity. Same reason the
/// payloadAttributes gasLimit / minBaseFee paths below wrap their fromQuantity calls:
/// fromQuantity throws std::invalid_argument, which the RPC entry point funnels into
/// -32603 InternalError, and malformed client input has to be -32602 instead.
uint64_t parseQuantity(Json::Value const& value, std::string_view field)
{
    if (value.isString())
    {
        if (auto parsed = bcos::safeFromQuantity(value.asString()))
        {
            return *parsed;
        }
    }
    BOOST_THROW_EXCEPTION(bcos::rpc::JsonRpcException(
        bcos::rpc::InvalidParams, std::string(field) + " must be a uint64 hex quantity string"));
}

/// minBaseFee-only reader. op-node declares PayloadAttributes.MinBaseFee as a plain
/// `*uint64` with `json:"minBaseFee,omitempty"` (op-service/eth/types.go:523, v1.19.3) —
/// unlike every sibling quantity field, it is NOT wrapped in hexutil, so op-node puts a
/// bare JSON number on the wire ("minBaseFee": 0). Accept exactly that extra form here,
/// on top of the usual hex quantity string. The number path must never go through
/// asString(): jsoncpp stringifies the number 10 to "10", which the hex reader would
/// then parse as 0x10 = 16 — a silently forged value. Reals (1.5, and 10.0 too) never
/// carry the int/uint JSON types, so they fall through to the rejection.
uint64_t parseQuantityOrUInt64Number(Json::Value const& value, std::string_view field)
{
    if (value.type() == Json::uintValue || (value.type() == Json::intValue && value.asInt64() >= 0))
    {
        return value.asUInt64();
    }
    if (value.isString())
    {
        if (auto parsed = bcos::safeFromQuantity(value.asString()))
        {
            return *parsed;
        }
    }
    BOOST_THROW_EXCEPTION(bcos::rpc::JsonRpcException(bcos::rpc::InvalidParams,
        std::string(field) +
            " must be a uint64 hex quantity string or a non-negative JSON integer"));
}

/// The owning object of a group of fields (executionPayload, payloadAttributes,
/// forkchoiceState). jsoncpp's operator[](char const*) throws Json::LogicError when the
/// value is not an object, and that would surface as -32603 InternalError for input that
/// is plainly a client error.
void requireObject(Json::Value const& value, std::string_view name)
{
    if (!value.isObject())
    {
        BOOST_THROW_EXCEPTION(bcos::rpc::JsonRpcException(
            bcos::rpc::InvalidParams, std::string(name) + " must be an object"));
    }
}

/// Strict read of a 32-byte hash field, over parseH256. The isString() gate is the part
/// parseH256 cannot do on its own: jsoncpp's asString() stringifies a number instead of
/// rejecting it, and THROWS Json::LogicError on an array/object — -32603 for what is a
/// malformed request. Named per field so the error says which one.
bcos::h256 parseH256Field(Json::Value const& value, std::string_view field)
{
    if (!value.isString())
    {
        BOOST_THROW_EXCEPTION(bcos::rpc::JsonRpcException(
            bcos::rpc::InvalidParams, std::string(field) + " must be a 32-byte hex string"));
    }
    return bcos::rpc::parseH256(value.asString());
}

/// The 20-byte counterpart of parseH256Field, over parseAddress.
bcos::Address parseAddressField(Json::Value const& value, std::string_view field)
{
    if (!value.isString())
    {
        BOOST_THROW_EXCEPTION(bcos::rpc::JsonRpcException(
            bcos::rpc::InvalidParams, std::string(field) + " must be a 20-byte hex string"));
    }
    return bcos::rpc::parseAddress(value.asString());
}

/// Variable-length hex bytes (extraData, logsBloom, eip1559Params). Same isString() gate,
/// plus fromHex's BadHexCharacter mapped to InvalidParams instead of InternalError.
bcos::bytes parseHexBytesField(Json::Value const& value, std::string_view field)
{
    auto reject = [&]() -> bcos::bytes {
        BOOST_THROW_EXCEPTION(bcos::rpc::JsonRpcException(
            bcos::rpc::InvalidParams, std::string(field) + " must be a hex string"));
    };
    if (!value.isString())
    {
        return reject();
    }
    try
    {
        return bcos::fromHex(value.asString());
    }
    catch (bcos::BadHexCharacter const&)
    {
        return reject();
    }
}

/// One element of a `withdrawals` array. The isObject() gate comes first because jsoncpp's
/// operator[](char const*) throws Json::LogicError on a non-object element (a bare string
/// in the list, say) — -32603 for what is plainly malformed client input.
bcos::engine::WithdrawalV1 parseWithdrawal(Json::Value const& withdrawal, std::string_view owner)
{
    auto field = [&](char const* name) { return std::string(owner) + ".withdrawals[]." + name; };
    if (!withdrawal.isObject())
    {
        BOOST_THROW_EXCEPTION(bcos::rpc::JsonRpcException(
            bcos::rpc::InvalidParams, std::string(owner) + ".withdrawals[] must be an object"));
    }
    if (!withdrawal["address"].isString())
    {
        BOOST_THROW_EXCEPTION(bcos::rpc::JsonRpcException(
            bcos::rpc::InvalidParams, field("address") + " must be a hex string"));
    }
    return bcos::engine::WithdrawalV1{
        .index = parseBigQuantity(withdrawal["index"], field("index")),
        .validatorIndex = parseBigQuantity(withdrawal["validatorIndex"], field("validatorIndex")),
        .amount = parseBigQuantity(withdrawal["amount"], field("amount")),
        .address = bcos::rpc::parseAddress(withdrawal["address"].asString()),
    };
}

/// engine_newPayloadV4 param shape, enforced before anything is read out of `params`.
/// op-geth's NewPayloadV4 (eth/catalyst/api.go:743-761) takes all four arguments and
/// answers InvalidParams for a nil beacon root or a nil executionRequests list; a
/// non-array/non-string argument never even reaches its handler because Go's JSON-RPC
/// codec rejects it. Checking up front matters for `params[2]`: the V3 block below
/// consumes it with parseH256, so a numeric beacon root would otherwise surface as
/// parseH256's "Expected hex string" instead of the shape error.
void requireNewPayloadV4ParamShape(Json::Value const& params)
{
    if (params.size() < 4 || !params[1].isArray() || !params[2].isString() || !params[3].isArray())
    {
        BOOST_THROW_EXCEPTION(bcos::rpc::JsonRpcException(bcos::rpc::InvalidParams,
            "engine_newPayloadV4 expects [executionPayload, expectedBlobVersionedHashes, "
            "parentBeaconBlockRoot, executionRequests]"));
    }
}

/// ExecutionPayloadV4 required fields. op-geth's NewPayloadV4 rejects a nil
/// withdrawals / blobGasUsed / excessBlobGas with -32602 (eth/catalyst/api.go:745-750).
/// withdrawalsRoot is required by Isthmus, where op-geth reports it as an INVALID payload
/// status instead (beacon/engine/types.go:320-323, reached via ExecutableDataToBlock);
/// this implementation is deliberately one notch stricter and answers -32602 for all four,
/// because a V4 payload missing any of them is malformed rather than merely unacceptable.
/// The engine service still carries its own INVALID-status withdrawalsRoot rule for
/// in-process callers (EngineServiceImpl.cpp validateExecutionPayload).
///
/// Types are checked, not just presence. Both matter and neither is theoretical:
/// jsoncpp iterates a non-array value as an EMPTY range, so a string `withdrawals` would
/// parse into a valid-looking empty list (fail open on the one field Isthmus constrains);
/// and asString() on an array/object throws Json::LogicError, which would leave the parse
/// as -32603 InternalError rather than naming the offending field.
/// The caller has already run requireObject on `ep`.
void requireExecutionPayloadV4Fields(Json::Value const& ep)
{
    if (!ep.isMember("withdrawals") || !ep["withdrawals"].isArray())
    {
        BOOST_THROW_EXCEPTION(bcos::rpc::JsonRpcException(bcos::rpc::InvalidParams,
            "executionPayload.withdrawals must be an array for ExecutionPayloadV4"));
    }
    // Emptiness of `withdrawals` is a payload-validity question judged by the engine
    // service; only its presence and type are shape questions and belong here.
    for (auto const* field : {"blobGasUsed", "excessBlobGas", "withdrawalsRoot"})
    {
        if (!ep.isMember(field) || !ep[field].isString())
        {
            BOOST_THROW_EXCEPTION(bcos::rpc::JsonRpcException(
                bcos::rpc::InvalidParams, std::string("executionPayload.") + field +
                                              " must be a hex string for ExecutionPayloadV4"));
        }
    }
}

// The Engine API speaks Unix seconds (execution-apis spec; op-node sends seconds), while
// FISCO-BCOS block headers store milliseconds (the executor divides by 1000 before handing
// the timestamp to the EVM, see BCOS2Evmone.cpp blockHeaderToBlockInfo). Convert at the RPC
// boundary only: parse multiplies by 1000, serialize divides by 1000; everything behind the
// boundary (PayloadAttributes / ExecutionPayload / block headers) stays in milliseconds.
constexpr uint64_t c_millisPerSecond = 1000;

uint64_t engineSecondsToInternalMillis(uint64_t seconds)
{
    // Bound by int64, not uint64: the converted value lands in
    // BlockHeader::setTimestamp(static_cast<int64_t>(...)) and headers store the
    // timestamp as int64_t milliseconds, so any seconds value whose millisecond
    // form exceeds INT64_MAX would silently wrap to a negative header timestamp.
    if (seconds > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) / c_millisPerSecond)
    {
        BOOST_THROW_EXCEPTION(bcos::rpc::JsonRpcException(bcos::rpc::InvalidParams,
            "timestamp exceeds representable range: " + std::to_string(seconds)));
    }
    return seconds * c_millisPerSecond;
}

uint64_t internalMillisToEngineSeconds(uint64_t millis)
{
    // Integer division truncates: sub-second precision is deliberately dropped at the
    // Engine boundary — execution-apis timestamps are second-granular, and blocks built
    // from Engine attributes carry whole-second internal timestamps anyway.
    return millis / c_millisPerSecond;
}
}  // namespace

bcos::engine::NewPayloadRequest bcos::rpc::parseNewPayloadRequest(
    Json::Value const& params, engine::ApiVersion version)
{
    // A missing params[0] reads back as a JSON null, which requireObject rejects with the
    // same -32602 as a wrongly-typed one.
    auto const& ep = params[0u];
    requireObject(ep, "executionPayload");
    if (version >= engine::ApiVersion::V4)
    {
        // Both gates run first, so the blocks below may assume the V4-specific parameters
        // and payload fields are present and correctly typed. Fields shared with V1-V3
        // (parentHash, gasLimit, ...) are not covered here and keep their existing
        // per-field handling.
        requireNewPayloadV4ParamShape(params);
        requireExecutionPayloadV4Fields(ep);
    }
    bcos::engine::ExecutionPayload payload{
        .logsBloom = {},
        .parentHash = parseH256Field(ep["parentHash"], "executionPayload.parentHash"),
        .stateRoot = parseH256Field(ep["stateRoot"], "executionPayload.stateRoot"),
        .receiptsRoot = parseH256Field(ep["receiptsRoot"], "executionPayload.receiptsRoot"),
        .prevRandao = parseH256Field(ep["prevRandao"], "executionPayload.prevRandao"),
        .gasLimit = parseBigQuantity(ep["gasLimit"], "executionPayload.gasLimit"),
        .gasUsed = parseBigQuantity(ep["gasUsed"], "executionPayload.gasUsed"),
        .baseFeePerGas = parseBigQuantity(ep["baseFeePerGas"], "executionPayload.baseFeePerGas"),
        .blockHash = parseH256Field(ep["blockHash"], "executionPayload.blockHash"),
        .transactions = {},
        .extraData = {},
        .feeRecipient = parseAddressField(ep["feeRecipient"], "executionPayload.feeRecipient"),
        .timestamp = engineSecondsToInternalMillis(
            parseQuantity(ep["timestamp"], "executionPayload.timestamp")),
        .blockNumber = static_cast<bcos::protocol::BlockNumber>(
            parseQuantity(ep["blockNumber"], "executionPayload.blockNumber")),
        .withdrawals = std::nullopt,
        .blobGasUsed = std::nullopt,
        .excessBlobGas = std::nullopt,
        .withdrawalsRoot = std::nullopt,
    };
    if (ep.isMember("extraData"))
    {
        payload.extraData = parseHexBytesField(ep["extraData"], "executionPayload.extraData");
    }
    if (ep.isMember("logsBloom"))
    {
        auto bloomBytes = parseHexBytesField(ep["logsBloom"], "executionPayload.logsBloom");
        if (bloomBytes.size() != BloomBytesSize)
        {
            BOOST_THROW_EXCEPTION(
                JsonRpcException(InvalidParams, "Expected 256-byte hex string for logsBloom, got " +
                                                    std::to_string(bloomBytes.size()) + " bytes"));
        }
        std::copy(bloomBytes.begin(), bloomBytes.end(), payload.logsBloom.begin());
    }
    if (ep.isMember("transactions") && !ep["transactions"].isNull())
    {
        if (!ep["transactions"].isArray())
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(
                InvalidParams, "Expected array of hex strings for executionPayload.transactions"));
        }
        // Raw EIP-2718 bytes, hex-decoded verbatim. No transaction decoding happens
        // here — getPayload must later return exactly these bytes.
        payload.transactions.reserve(ep["transactions"].size());
        for (Json::ArrayIndex i = 0; i < ep["transactions"].size(); ++i)
        {
            payload.transactions.push_back(bcos::engine::EngineTransaction{
                .raw = parseRawTransactionElement(ep["transactions"][i], "executionPayload", i),
                .decoded = nullptr,
            });
        }
    }
    if (ep.isMember("withdrawals") && !ep["withdrawals"].isNull())
    {
        // jsoncpp iterates a non-array value as an EMPTY range, so without this gate a
        // string `withdrawals` would parse into a valid-looking empty list.
        if (!ep["withdrawals"].isArray())
        {
            BOOST_THROW_EXCEPTION(
                JsonRpcException(InvalidParams, "executionPayload.withdrawals must be an array"));
        }
        std::vector<bcos::engine::WithdrawalV1> withdrawals;
        for (auto const& w : ep["withdrawals"])
        {
            withdrawals.push_back(parseWithdrawal(w, "executionPayload"));
        }
        payload.withdrawals = std::move(withdrawals);
    }
    if (ep.isMember("blobGasUsed"))
    {
        payload.blobGasUsed = parseBigQuantity(ep["blobGasUsed"], "executionPayload.blobGasUsed");
    }
    if (ep.isMember("excessBlobGas"))
    {
        payload.excessBlobGas =
            parseBigQuantity(ep["excessBlobGas"], "executionPayload.excessBlobGas");
    }
    // withdrawalsRoot is an ExecutionPayloadV4+ field (Isthmus), required there. For
    // V1-V3 requests it is ignored rather than rejected: the Isthmus spec leaves pre-V4
    // handling unspecified and op-geth's NewPayloadV3 (eth/catalyst/api.go) performs no
    // withdrawalsRoot validation, so rejecting here would diverge from op-geth.
    if (version >= engine::ApiVersion::V4)
    {
        // Presence and string-ness were already enforced by requireExecutionPayloadV4Fields.
        payload.withdrawalsRoot = parseH256(ep["withdrawalsRoot"].asString());
    }

    bcos::engine::NewPayloadRequest request{
        .executionPayload = std::move(payload),
        .expectedBlobVersionedHashes = {},
        .parentBeaconBlockRoot = std::nullopt,
        .executionRequests = std::nullopt,
    };
    if (version >= engine::ApiVersion::V3 && params.size() >= 2 && !params[1].isNull())
    {
        // Reject a wrongly-typed params[1] rather than skipping the block: gating the
        // whole read on isArray() fails OPEN, leaving the list empty and letting
        // `[payload, "notarray", beaconRoot]` through as a valid V3 request. V4 is covered
        // by requireNewPayloadV4ParamShape; this is the V1-V3 half of the same gate.
        if (!params[1].isArray())
        {
            BOOST_THROW_EXCEPTION(
                JsonRpcException(InvalidParams, "expectedBlobVersionedHashes must be an array"));
        }
        // Element type is checked, not just the array type: a non-string element reaches
        // parseH256's asString(), which throws Json::LogicError on an array/object
        // (-32603) and silently stringifies a number.
        for (Json::ArrayIndex i = 0; i < params[1].size(); ++i)
        {
            request.expectedBlobVersionedHashes.push_back(parseH256Field(
                params[1][i], "expectedBlobVersionedHashes[" + std::to_string(i) + "]"));
        }
    }
    if (version >= engine::ApiVersion::V3 && params.size() >= 3 && !params[2].isNull())
    {
        request.parentBeaconBlockRoot = parseH256Field(params[2], "parentBeaconBlockRoot");
    }
    if (version >= engine::ApiVersion::V4)
    {
        // Shape already validated at the top of this function; emptiness of the two lists
        // is a payload-validity question and is judged by the engine service, not here.
        std::vector<bytes> executionRequests;
        executionRequests.reserve(params[3].size());
        for (auto const& item : params[3])
        {
            if (!item.isString())
            {
                BOOST_THROW_EXCEPTION(JsonRpcException(
                    InvalidParams, "executionRequests entries must be hex strings"));
            }
            try
            {
                executionRequests.push_back(fromHex(item.asString()));
            }
            catch (bcos::BadHexCharacter const&)
            {
                BOOST_THROW_EXCEPTION(JsonRpcException(
                    InvalidParams, "executionRequests entries must be hex strings"));
            }
        }
        request.executionRequests = std::move(executionRequests);
    }
    return request;
}

Json::Value bcos::rpc::serializePayloadStatus(
    bcos::engine::PayloadStatus const& status, engine::ApiVersion version)
{
    Json::Value result(Json::objectValue);
    switch (status.status)
    {
    case bcos::engine::PayloadValidationStatus::Valid:
        result["status"] = "VALID";
        break;
    case bcos::engine::PayloadValidationStatus::Invalid:
        result["status"] = "INVALID";
        break;
    case bcos::engine::PayloadValidationStatus::Syncing:
        result["status"] = "SYNCING";
        break;
    case bcos::engine::PayloadValidationStatus::Accepted:
        result["status"] = "ACCEPTED";
        break;
    case bcos::engine::PayloadValidationStatus::InvalidBlockHash:
        result["status"] = (version >= engine::ApiVersion::V3) ? "INVALID" : "INVALID_BLOCK_HASH";
        break;
    }
    if (status.latestValidHash.has_value())
    {
        result["latestValidHash"] = status.latestValidHash->hexPrefixed();
    }
    if (status.validationError.has_value())
    {
        result["validationError"] = *status.validationError;
    }
    return result;
}

std::optional<bcos::engine::PayloadAttributes> bcos::rpc::parsePayloadAttributes(
    Json::Value const& params, engine::ApiVersion version)
{
    if (params.size() < 2 || params[1].isNull())
    {
        return std::nullopt;
    }
    auto const& pa = params[1];
    requireObject(pa, "payloadAttributes");
    bcos::engine::PayloadAttributes attrs{
        .prevRandao = parseH256Field(pa["prevRandao"], "payloadAttributes.prevRandao"),
        .suggestedFeeRecipient = parseAddressField(
            pa["suggestedFeeRecipient"], "payloadAttributes.suggestedFeeRecipient"),
        // parseQuantity, not fromQuantity(asString()): this is the live
        // engine_forkchoiceUpdatedV3 entry point, so an unstrict read here is the one an
        // external op-node can actually hit. asString() stringifies a JSON number, which
        // fromQuantity then reads as HEX (`"timestamp": 123` -> 0x123), and fromQuantity
        // reports malformed hex as std::invalid_argument -> -32603 instead of -32602.
        .timestamp = engineSecondsToInternalMillis(
            parseQuantity(pa["timestamp"], "payloadAttributes.timestamp")),
        .withdrawals = std::nullopt,
        .parentBeaconBlockRoot = std::nullopt,
        .transactions = std::nullopt,
        .noTxPool = std::nullopt,
        .gasLimit = std::nullopt,
        .eip1559Params = std::nullopt,
        .minBaseFee = std::nullopt,
    };
    if (pa.isMember("withdrawals") && !pa["withdrawals"].isNull())
    {
        if (!pa["withdrawals"].isArray())
        {
            BOOST_THROW_EXCEPTION(
                JsonRpcException(InvalidParams, "payloadAttributes.withdrawals must be an array"));
        }
        std::vector<bcos::engine::WithdrawalV1> withdrawals;
        for (auto const& w : pa["withdrawals"])
        {
            withdrawals.push_back(parseWithdrawal(w, "payloadAttributes"));
        }
        attrs.withdrawals = std::move(withdrawals);
    }
    if (pa.isMember("parentBeaconBlockRoot") && !pa["parentBeaconBlockRoot"].isNull())
    {
        attrs.parentBeaconBlockRoot =
            parseH256Field(pa["parentBeaconBlockRoot"], "payloadAttributes.parentBeaconBlockRoot");
    }
    // OP Stack attributes. Raw transaction hex strings are stored verbatim without
    // decoding — the Engine transaction codec consumes them downstream.
    if (pa.isMember("transactions") && !pa["transactions"].isNull())
    {
        if (!pa["transactions"].isArray())
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(
                InvalidParams, "Expected array of hex strings for payloadAttributes.transactions"));
        }
        std::vector<std::string> transactions;
        transactions.reserve(pa["transactions"].size());
        for (Json::ArrayIndex i = 0; i < pa["transactions"].size(); ++i)
        {
            // Validate shape and hex, but keep the original string verbatim — the
            // forced-transaction list must pass through byte-identical.
            parseRawTransactionElement(pa["transactions"][i], "payloadAttributes", i);
            transactions.push_back(pa["transactions"][i].asString());
        }
        attrs.transactions = std::move(transactions);
    }
    if (pa.isMember("noTxPool") && !pa["noTxPool"].isNull())
    {
        // Strict: jsoncpp asBool() silently converts numbers/strings, so gate on the
        // JSON type (op-node serializes NoTxPool as a JSON bool).
        if (!pa["noTxPool"].isBool())
        {
            BOOST_THROW_EXCEPTION(
                JsonRpcException(InvalidParams, "Expected boolean for payloadAttributes.noTxPool"));
        }
        attrs.noTxPool = pa["noTxPool"].asBool();
    }
    // The three fields below go through the strict readers rather than a bare
    // fromQuantity / fromHex: those stringify a JSON number before parsing it as hex (a
    // forged value) and report malformed input as std::invalid_argument, which the RPC
    // entry point would surface as -32603 InternalError.
    //
    // KNOWN GAP: gasLimit is parsed and carried on PayloadAttributes but buildPayload
    // IGNORES it — the built block's gas limit always comes from this chain's own
    // SystemConfig (EngineServiceImpl.h, ledgerConfig.gasLimit()). op-geth does the
    // opposite: the attribute is mandatory on an OP chain (checkOptimismPayloadAttributes,
    // eth/catalyst/api_optimism.go:41-43, else -38003) and sets header.GasLimit verbatim
    // (miner/worker.go:362-363), and op-node always sends it from the L1 SystemConfig
    // (op-node/rollup/derive/attributes.go:207-215). Honouring it changes what block this
    // node produces, which belongs to the header-fields work rather than to this
    // method-surface change.
    if (pa.isMember("gasLimit") && !pa["gasLimit"].isNull())
    {
        attrs.gasLimit = parseQuantity(pa["gasLimit"], "payloadAttributes.gasLimit");
    }
    if (pa.isMember("eip1559Params") && !pa["eip1559Params"].isNull())
    {
        auto paramsBytes =
            parseHexBytesField(pa["eip1559Params"], "payloadAttributes.eip1559Params");
        if (paramsBytes.size() != c_eip1559ParamsBytes)
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(
                InvalidParams, "Expected 8-byte hex string for eip1559Params, got " +
                                   std::to_string(paramsBytes.size()) + " bytes"));
        }
        attrs.eip1559Params = std::move(paramsBytes);
    }
    if (pa.isMember("minBaseFee") && !pa["minBaseFee"].isNull())
    {
        // Bare-number form accepted alongside hex: op-node serializes MinBaseFee
        // without hexutil (op-service/eth/types.go:523), so every post-Jovian FCU
        // carries e.g. "minBaseFee": 0 and a hex-only reader rejects it with -32602.
        attrs.minBaseFee =
            parseQuantityOrUInt64Number(pa["minBaseFee"], "payloadAttributes.minBaseFee");
    }
    return attrs;
}

Json::Value bcos::rpc::serializePayloadAttributes(bcos::engine::PayloadAttributes const& attrs)
{
    Json::Value pa(Json::objectValue);
    // Inverse of parsePayloadAttributes: attrs.timestamp is internal milliseconds,
    // the Engine-facing JSON carries Unix seconds.
    pa["timestamp"] = toQuantity(internalMillisToEngineSeconds(attrs.timestamp));
    pa["prevRandao"] = attrs.prevRandao.hexPrefixed();
    pa["suggestedFeeRecipient"] = attrs.suggestedFeeRecipient.hexPrefixed();
    if (attrs.withdrawals.has_value())
    {
        Json::Value withdrawals(Json::arrayValue);
        for (auto const& w : *attrs.withdrawals)
        {
            Json::Value item(Json::objectValue);
            item["index"] = toQuantity(w.index);
            item["validatorIndex"] = toQuantity(w.validatorIndex);
            item["address"] = w.address.hexPrefixed();
            item["amount"] = toQuantity(w.amount);
            withdrawals.append(std::move(item));
        }
        pa["withdrawals"] = std::move(withdrawals);
    }
    if (attrs.parentBeaconBlockRoot.has_value())
    {
        pa["parentBeaconBlockRoot"] = attrs.parentBeaconBlockRoot->hexPrefixed();
    }
    if (attrs.transactions.has_value())
    {
        Json::Value transactions(Json::arrayValue);
        for (auto const& transaction : *attrs.transactions)
        {
            transactions.append(transaction);
        }
        pa["transactions"] = std::move(transactions);
    }
    if (attrs.noTxPool.has_value())
    {
        pa["noTxPool"] = *attrs.noTxPool;
    }
    if (attrs.gasLimit.has_value())
    {
        pa["gasLimit"] = toQuantity(*attrs.gasLimit);
    }
    if (attrs.eip1559Params.has_value())
    {
        pa["eip1559Params"] = toHexStringWithPrefix(*attrs.eip1559Params);
    }
    if (attrs.minBaseFee.has_value())
    {
        pa["minBaseFee"] = toQuantity(*attrs.minBaseFee);
    }
    return pa;
}

bcos::engine::ForkchoiceState bcos::rpc::parseForkchoiceState(Json::Value const& params)
{
    // A missing params[0] reads back as a JSON null, which requireObject rejects with the
    // same -32602 as a wrongly-typed one.
    auto const& fc = params[0u];
    requireObject(fc, "forkchoiceState");
    return bcos::engine::ForkchoiceState{
        .headBlockHash = parseH256Field(fc["headBlockHash"], "forkchoiceState.headBlockHash"),
        .safeBlockHash = parseH256Field(fc["safeBlockHash"], "forkchoiceState.safeBlockHash"),
        .finalizedBlockHash =
            parseH256Field(fc["finalizedBlockHash"], "forkchoiceState.finalizedBlockHash"),
    };
}

Json::Value bcos::rpc::combineForkchoiceUpdatedResult(
    bcos::engine::ForkchoiceUpdatedResult const& result, engine::ApiVersion version)
{
    Json::Value jsonResult(Json::objectValue);
    jsonResult["payloadStatus"] = serializePayloadStatus(result.payloadStatus, version);
    if (result.payloadId.has_value())
    {
        jsonResult["payloadId"] = *result.payloadId;
    }
    return jsonResult;
}

Json::Value bcos::rpc::serializeExecutionPayload(
    bcos::engine::ExecutionPayload const& payload, engine::ApiVersion version)
{
    Json::Value ep(Json::objectValue);
    ep["parentHash"] = payload.parentHash.hexPrefixed();
    ep["feeRecipient"] = payload.feeRecipient.hexPrefixed();
    ep["stateRoot"] = payload.stateRoot.hexPrefixed();
    ep["receiptsRoot"] = payload.receiptsRoot.hexPrefixed();
    ep["logsBloom"] = toPaddingHexStringWithPrefix(BloomBytesSize, payload.logsBloom);
    ep["prevRandao"] = payload.prevRandao.hexPrefixed();
    ep["blockNumber"] = toQuantity(payload.blockNumber);
    ep["gasLimit"] = toQuantity(payload.gasLimit);
    ep["gasUsed"] = toQuantity(payload.gasUsed);
    ep["timestamp"] = toQuantity(internalMillisToEngineSeconds(payload.timestamp));
    ep["extraData"] = toHexStringWithPrefix(payload.extraData);
    ep["baseFeePerGas"] = toQuantity(payload.baseFeePerGas);
    ep["blockHash"] = payload.blockHash.hexPrefixed();

    Json::Value transactions(Json::arrayValue);
    for (auto const& transaction : payload.transactions)
    {
        // Raw EIP-2718 bytes out, exactly as carried — byte-for-byte what newPayload
        // received or what buildPayload reassembled.
        transactions.append(toHexStringWithPrefix(transaction.raw));
    }
    ep["transactions"] = std::move(transactions);

    if (payload.withdrawals.has_value())
    {
        Json::Value withdrawals(Json::arrayValue);
        for (auto const& w : *payload.withdrawals)
        {
            Json::Value item(Json::objectValue);
            item["index"] = toQuantity(w.index);
            item["validatorIndex"] = toQuantity(w.validatorIndex);
            item["address"] = w.address.hexPrefixed();
            item["amount"] = toQuantity(w.amount);
            withdrawals.append(std::move(item));
        }
        ep["withdrawals"] = std::move(withdrawals);
    }
    if (payload.blobGasUsed.has_value())
    {
        ep["blobGasUsed"] = toQuantity(*payload.blobGasUsed);
    }
    if (payload.excessBlobGas.has_value())
    {
        ep["excessBlobGas"] = toQuantity(*payload.excessBlobGas);
    }
    // V4+ only (Isthmus): a required field of the payload shape, never emitted in V1-V3.
    // Absence here is an internal invariant violation, not a client error: every payload
    // that can reach a V4+ serialization was either built locally (buildPayload sets the
    // field for V3+ builds, currently to the documented zero placeholder — see
    // EngineServiceImpl.h) or parsed from a V4 request (where the field is mandatory).
    // Fail loudly rather than emitting a zero root the CL would take for a real
    // L2ToL1MessagePasser storage root.
    if (version >= engine::ApiVersion::V4)
    {
        if (!payload.withdrawalsRoot.has_value())
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(InternalError,
                "withdrawalsRoot missing on an ExecutionPayloadV4+ built by this node"));
        }
        ep["withdrawalsRoot"] = payload.withdrawalsRoot->hexPrefixed();
    }
    return ep;
}

void bcos::rpc::combineGetPayloadResponse(Json::Value& _result,
    bcos::engine::GetPayloadResult const& _response, engine::ApiVersion version)
{
    if (version == engine::ApiVersion::V1)
    {
        _result = serializeExecutionPayload(_response->executionPayload, version);
        return;
    }

    _result["executionPayload"] = serializeExecutionPayload(_response->executionPayload, version);
    _result["blockValue"] = toQuantity(_response->blockValue);

    if (version == engine::ApiVersion::V2)
    {
        return;
    }

    // V3+: blobsBundle and shouldOverrideBuilder
    Json::Value blobsBundle(Json::objectValue);
    blobsBundle["commitments"] = Json::Value(Json::arrayValue);
    blobsBundle["proofs"] = Json::Value(Json::arrayValue);
    blobsBundle["blobs"] = Json::Value(Json::arrayValue);
    if (_response->blobsBundle.has_value())
    {
        Json::Value commitments(Json::arrayValue);
        for (auto const& commitment : _response->blobsBundle->commitments)
        {
            commitments.append(toHexStringWithPrefix(commitment));
        }
        blobsBundle["commitments"] = std::move(commitments);

        Json::Value proofs(Json::arrayValue);
        for (auto const& proof : _response->blobsBundle->proofs)
        {
            proofs.append(toHexStringWithPrefix(proof));
        }
        blobsBundle["proofs"] = std::move(proofs);

        Json::Value blobs(Json::arrayValue);
        for (auto const& blob : _response->blobsBundle->blobs)
        {
            blobs.append(toHexStringWithPrefix(blob));
        }
        blobsBundle["blobs"] = std::move(blobs);
    }
    _result["blobsBundle"] = std::move(blobsBundle);
    _result["shouldOverrideBuilder"] = _response->shouldOverrideBuilder;
    // V4/V5: executionRequests is part of the response shape; Karst always answers [].
    if (version >= engine::ApiVersion::V4)
    {
        Json::Value executionRequests(Json::arrayValue);
        if (_response->executionRequests.has_value())
        {
            for (auto const& executionRequest : *_response->executionRequests)
            {
                executionRequests.append(toHexStringWithPrefix(executionRequest));
            }
        }
        _result["executionRequests"] = std::move(executionRequests);
    }
    if (_response->parentBeaconBlockRoot.has_value())
    {
        _result["parentBeaconBlockRoot"] = _response->parentBeaconBlockRoot->hexPrefixed();
    }
}
