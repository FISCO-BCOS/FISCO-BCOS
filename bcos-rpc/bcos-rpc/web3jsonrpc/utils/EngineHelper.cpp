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
#include <bcos-framework/protocol/TransactionFactory.h>

using namespace bcos;
using namespace bcos::rpc;

namespace
{
/// Holocene eip1559Params: 4-byte denominator followed by 4-byte elasticity.
constexpr std::size_t c_eip1559ParamsBytes = 8;
}  // namespace

bcos::engine::NewPayloadRequest bcos::rpc::parseNewPayloadRequest(Json::Value const& params,
    bcos::protocol::TransactionFactory& transactionFactory, engine::ApiVersion version)
{
    auto const& ep = params[0u];
    bcos::engine::ExecutionPayload payload{
        .logsBloom = {},
        .parentHash = parseH256(ep["parentHash"].asString()),
        .stateRoot = parseH256(ep["stateRoot"].asString()),
        .receiptsRoot = parseH256(ep["receiptsRoot"].asString()),
        .prevRandao = parseH256(ep["prevRandao"].asString()),
        .gasLimit = fromBigQuantity(ep["gasLimit"].asString()),
        .gasUsed = fromBigQuantity(ep["gasUsed"].asString()),
        .baseFeePerGas = fromBigQuantity(ep["baseFeePerGas"].asString()),
        .blockHash = parseH256(ep["blockHash"].asString()),
        .transactions = {},
        .extraData = {},
        .feeRecipient = parseAddress(ep["feeRecipient"].asString()),
        .timestamp = fromQuantity(std::string(ep["timestamp"].asString())),
        .blockNumber =
            static_cast<bcos::protocol::BlockNumber>(fromQuantity(ep["blockNumber"].asString())),
        .withdrawals = std::nullopt,
        .blobGasUsed = std::nullopt,
        .excessBlobGas = std::nullopt,
        .withdrawalsRoot = std::nullopt,
    };
    if (ep.isMember("extraData"))
    {
        payload.extraData = fromHex(ep["extraData"].asString());
    }
    if (ep.isMember("logsBloom"))
    {
        auto bloomBytes = fromHex(ep["logsBloom"].asString());
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
        payload.transactions.reserve(ep["transactions"].size());
        for (auto const& tx : ep["transactions"])
        {
            auto txData = fromHex(tx.asString());
            payload.transactions.push_back(transactionFactory.decodeTransaction(ref(txData)));
        }
    }
    if (ep.isMember("withdrawals") && !ep["withdrawals"].isNull())
    {
        std::vector<bcos::engine::WithdrawalV1> withdrawals;
        for (auto const& w : ep["withdrawals"])
        {
            withdrawals.push_back(bcos::engine::WithdrawalV1{
                .index = fromBigQuantity(w["index"].asString()),
                .validatorIndex = fromBigQuantity(w["validatorIndex"].asString()),
                .amount = fromBigQuantity(w["amount"].asString()),
                .address = parseAddress(w["address"].asString()),
            });
        }
        payload.withdrawals = std::move(withdrawals);
    }
    if (ep.isMember("blobGasUsed"))
    {
        payload.blobGasUsed = fromBigQuantity(ep["blobGasUsed"].asString());
    }
    if (ep.isMember("excessBlobGas"))
    {
        payload.excessBlobGas = fromBigQuantity(ep["excessBlobGas"].asString());
    }
    if (ep.isMember("withdrawalsRoot") && !ep["withdrawalsRoot"].isNull())
    {
        payload.withdrawalsRoot = parseH256(ep["withdrawalsRoot"].asString());
    }

    bcos::engine::NewPayloadRequest request{
        .executionPayload = std::move(payload),
        .expectedBlobVersionedHashes = {},
        .parentBeaconBlockRoot = std::nullopt,
        .executionRequests = std::nullopt,
    };
    if (version >= engine::ApiVersion::V3 && params.size() >= 2 && params[1].isArray())
    {
        for (auto const& h : params[1])
        {
            request.expectedBlobVersionedHashes.push_back(parseH256(h.asString()));
        }
    }
    if (version >= engine::ApiVersion::V3 && params.size() >= 3 && !params[2].isNull())
    {
        request.parentBeaconBlockRoot = parseH256(params[2].asString());
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
    bcos::engine::PayloadAttributes attrs{
        .prevRandao = parseH256(pa["prevRandao"].asString()),
        .suggestedFeeRecipient = parseAddress(pa["suggestedFeeRecipient"].asString()),
        .timestamp = fromQuantity(std::string(pa["timestamp"].asString())),
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
        std::vector<bcos::engine::WithdrawalV1> withdrawals;
        for (auto const& w : pa["withdrawals"])
        {
            withdrawals.push_back(bcos::engine::WithdrawalV1{
                .index = fromBigQuantity(w["index"].asString()),
                .validatorIndex = fromBigQuantity(w["validatorIndex"].asString()),
                .amount = fromBigQuantity(w["amount"].asString()),
                .address = parseAddress(w["address"].asString()),
            });
        }
        attrs.withdrawals = std::move(withdrawals);
    }
    if (pa.isMember("parentBeaconBlockRoot") && !pa["parentBeaconBlockRoot"].isNull())
    {
        attrs.parentBeaconBlockRoot = parseH256(pa["parentBeaconBlockRoot"].asString());
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
        for (auto const& transaction : pa["transactions"])
        {
            transactions.push_back(transaction.asString());
        }
        attrs.transactions = std::move(transactions);
    }
    if (pa.isMember("noTxPool") && !pa["noTxPool"].isNull())
    {
        attrs.noTxPool = pa["noTxPool"].asBool();
    }
    if (pa.isMember("gasLimit") && !pa["gasLimit"].isNull())
    {
        attrs.gasLimit = fromQuantity(std::string(pa["gasLimit"].asString()));
    }
    if (pa.isMember("eip1559Params") && !pa["eip1559Params"].isNull())
    {
        auto paramsBytes = fromHex(pa["eip1559Params"].asString());
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
        attrs.minBaseFee = fromQuantity(std::string(pa["minBaseFee"].asString()));
    }
    return attrs;
}

Json::Value bcos::rpc::serializePayloadAttributes(bcos::engine::PayloadAttributes const& attrs)
{
    Json::Value pa(Json::objectValue);
    pa["timestamp"] = toQuantity(attrs.timestamp);
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
    auto const& fc = params[0u];
    return bcos::engine::ForkchoiceState{
        .headBlockHash = parseH256(fc["headBlockHash"].asString()),
        .safeBlockHash = parseH256(fc["safeBlockHash"].asString()),
        .finalizedBlockHash = parseH256(fc["finalizedBlockHash"].asString()),
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
    ep["timestamp"] = toQuantity(payload.timestamp);
    ep["extraData"] = toHexStringWithPrefix(payload.extraData);
    ep["baseFeePerGas"] = toQuantity(payload.baseFeePerGas);
    ep["blockHash"] = payload.blockHash.hexPrefixed();

    Json::Value transactions(Json::arrayValue);
    for (auto const& transaction : payload.transactions)
    {
        bytes encoded;
        transaction->encode(encoded);
        transactions.append(toHexStringWithPrefix(encoded));
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
    if (payload.withdrawalsRoot.has_value())
    {
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
    if (_response->parentBeaconBlockRoot.has_value())
    {
        _result["parentBeaconBlockRoot"] = _response->parentBeaconBlockRoot->hexPrefixed();
    }
}
