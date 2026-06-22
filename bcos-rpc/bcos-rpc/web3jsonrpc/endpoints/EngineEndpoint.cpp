/**
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file EngineEndpoint.cpp
 * @author: GitHub Copilot
 * @date 2026/5/7
 */

#include "EngineEndpoint.h"

#include <bcos-rpc/jsonrpc/Common.h>
#include <bcos-rpc/web3jsonrpc/utils/util.h>
#include <bcos-utilities/DataConvertUtility.h>

using namespace bcos;
using namespace bcos::rpc;

namespace
{
constexpr int32_t kEngineNotAvailable = -32000;

/// Build a JSON-RPC error response when the engine service is not available.
void buildEngineNotAvailableError(const Json::Value& request, Json::Value& response)
{
    buildJsonError(request, kEngineNotAvailable,
        "Engine service is not available on this node", response);
}

/// Parse a hex-prefixed string into h256.
h256 parseH256(std::string_view hex)
{
    auto bytes = fromHex(hex);
    if (bytes.size() != 32)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(
            InvalidParams, "Expected 32-byte hex string for h256, got " + std::to_string(bytes.size()) + " bytes"));
    }
    h256 result;
    std::copy(bytes.begin(), bytes.end(), result.begin());
    return result;
}

/// Parse a hex-prefixed string into Address.
Address parseAddress(std::string_view hex)
{
    auto bytes = fromHex(hex);
    if (bytes.size() != 20)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(
            InvalidParams, "Expected 20-byte hex string for address, got " + std::to_string(bytes.size()) + " bytes"));
    }
    Address result;
    std::copy(bytes.begin(), bytes.end(), result.begin());
    return result;
}

/// Parse forkchoice state from JSON params[0].
engine::ForkchoiceState parseForkchoiceState(const Json::Value& params)
{
    auto const& fc = params[0u];
    return engine::ForkchoiceState{
        .headBlockHash = parseH256(fc["headBlockHash"].asString()),
        .safeBlockHash = parseH256(fc["safeBlockHash"].asString()),
        .finalizedBlockHash = parseH256(fc["finalizedBlockHash"].asString()),
    };
}

/// Parse optional payload attributes from JSON params[1].
std::optional<engine::PayloadAttributes> parsePayloadAttributes(const Json::Value& params)
{
    if (params.size() < 2 || params[1].isNull())
    {
        return std::nullopt;
    }
    auto const& pa = params[1];
    engine::PayloadAttributes attrs{
        .timestamp = fromQuantity(std::string(pa["timestamp"].asString())),
        .prevRandao = parseH256(pa["prevRandao"].asString()),
        .suggestedFeeRecipient = parseAddress(pa["suggestedFeeRecipient"].asString()),
        .withdrawals = std::nullopt,
        .parentBeaconBlockRoot = std::nullopt,
    };
    if (pa.isMember("withdrawals") && !pa["withdrawals"].isNull())
    {
        std::vector<engine::WithdrawalV1> withdrawals;
        for (auto const& w : pa["withdrawals"])
        {
            withdrawals.push_back(engine::WithdrawalV1{
                .index = fromBigQuantity(w["index"].asString()),
                .validatorIndex = fromBigQuantity(w["validatorIndex"].asString()),
                .address = parseAddress(w["address"].asString()),
                .amount = fromBigQuantity(w["amount"].asString()),
            });
        }
        attrs.withdrawals = std::move(withdrawals);
    }
    if (pa.isMember("parentBeaconBlockRoot") && !pa["parentBeaconBlockRoot"].isNull())
    {
        attrs.parentBeaconBlockRoot = parseH256(pa["parentBeaconBlockRoot"].asString());
    }
    return attrs;
}

/// Parse newPayload request from JSON params.
engine::NewPayloadRequest parseNewPayloadRequest(
    const Json::Value& params, bcos::protocol::TransactionFactory& transactionFactory)
{
    auto const& ep = params[0u];
    engine::ExecutionPayload payload{
        .parentHash = parseH256(ep["parentHash"].asString()),
        .feeRecipient = parseAddress(ep["feeRecipient"].asString()),
        .stateRoot = parseH256(ep["stateRoot"].asString()),
        .receiptsRoot = parseH256(ep["receiptsRoot"].asString()),
        .logsBloom = {},
        .prevRandao = parseH256(ep["prevRandao"].asString()),
        .blockNumber = static_cast<bcos::protocol::BlockNumber>(
            fromQuantity(std::string(ep["blockNumber"].asString()))),
        .gasLimit = fromBigQuantity(ep["gasLimit"].asString()),
        .gasUsed = fromBigQuantity(ep["gasUsed"].asString()),
        .timestamp = fromQuantity(std::string(ep["timestamp"].asString())),
        .extraData = {},
        .baseFeePerGas = fromBigQuantity(ep["baseFeePerGas"].asString()),
        .blockHash = parseH256(ep["blockHash"].asString()),
        .transactions = {},
        .withdrawals = std::nullopt,
        .blobGasUsed = std::nullopt,
        .excessBlobGas = std::nullopt,
    };
    // extraData
    if (ep.isMember("extraData"))
    {
        payload.extraData = fromHex(ep["extraData"].asString());
    }
    // logsBloom
    if (ep.isMember("logsBloom"))
    {
        auto bloomBytes = fromHex(ep["logsBloom"].asString());
        if (bloomBytes.size() != BloomBytesSize)
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams,
                "Expected 256-byte hex string for logsBloom, got " +
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

    // withdrawals
    if (ep.isMember("withdrawals") && !ep["withdrawals"].isNull())
    {
        std::vector<engine::WithdrawalV1> withdrawals;
        for (auto const& w : ep["withdrawals"])
        {
            withdrawals.push_back(engine::WithdrawalV1{
                .index = fromBigQuantity(w["index"].asString()),
                .validatorIndex = fromBigQuantity(w["validatorIndex"].asString()),
                .address = parseAddress(w["address"].asString()),
                .amount = fromBigQuantity(w["amount"].asString()),
            });
        }
        payload.withdrawals = std::move(withdrawals);
    }
    // blobGasUsed / excessBlobGas
    if (ep.isMember("blobGasUsed"))
    {
        payload.blobGasUsed = fromBigQuantity(ep["blobGasUsed"].asString());
    }
    if (ep.isMember("excessBlobGas"))
    {
        payload.excessBlobGas = fromBigQuantity(ep["excessBlobGas"].asString());
    }

    engine::NewPayloadRequest request{
        .executionPayload = std::move(payload),
        .expectedBlobVersionedHashes = {},
        .parentBeaconBlockRoot = std::nullopt,
    };

    // expectedBlobVersionedHashes
    if (params.size() >= 2 && params[1].isArray())
    {
        for (auto const& h : params[1])
        {
            request.expectedBlobVersionedHashes.push_back(parseH256(h.asString()));
        }
    }
    // parentBeaconBlockRoot
    if (params.size() >= 3 && !params[2].isNull())
    {
        request.parentBeaconBlockRoot = parseH256(params[2].asString());
    }
    return request;
}

/// Serialize PayloadStatus to JSON.
Json::Value serializePayloadStatus(const engine::PayloadStatus& status)
{
    Json::Value result(Json::objectValue);
    result["status"] = [&] {
        switch (status.status)
        {
        case engine::PayloadValidationStatus::Valid:
            return "VALID";
        case engine::PayloadValidationStatus::Invalid:
            return "INVALID";
        case engine::PayloadValidationStatus::Syncing:
            return "SYNCING";
        case engine::PayloadValidationStatus::Accepted:
            return "ACCEPTED";
        case engine::PayloadValidationStatus::InvalidBlockHash:
            return "INVALID_BLOCK_HASH";
        }
        return "SYNCING";
    }();
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

/// Serialize ForkchoiceUpdatedResult to JSON.
Json::Value serializForkchoiceUpdatedResult(const engine::ForkchoiceUpdatedResult& result)
{
    Json::Value json(Json::objectValue);
    json["payloadStatus"] = serializePayloadStatus(result.payloadStatus);
    if (result.payloadId.has_value())
    {
        json["payloadId"] = *result.payloadId;
    }
    return json;
}

/// Serialize ExecutionPayload to JSON.
Json::Value serializeExecutionPayload(const engine::ExecutionPayload& payload)
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
        Json::Value ws(Json::arrayValue);
        for (auto const& w : *payload.withdrawals)
        {
            Json::Value wj(Json::objectValue);
            wj["index"] = toQuantity(w.index);
            wj["validatorIndex"] = toQuantity(w.validatorIndex);
            wj["address"] = w.address.hexPrefixed();
            wj["amount"] = toQuantity(w.amount);
            ws.append(std::move(wj));
        }
        ep["withdrawals"] = std::move(ws);
    }
    if (payload.blobGasUsed.has_value())
    {
        ep["blobGasUsed"] = toQuantity(*payload.blobGasUsed);
    }
    if (payload.excessBlobGas.has_value())
    {
        ep["excessBlobGas"] = toQuantity(*payload.excessBlobGas);
    }
    return ep;
}
}  // namespace

EngineEndpoint::EngineEndpoint(NodeService::Ptr nodeService) : m_nodeService(std::move(nodeService))
{}

task::Task<void> EngineEndpoint::exchangeCapabilities(
    const Json::Value& request, Json::Value& response)
{
    auto& engineService = m_nodeService->engineService();
    if (!engineService)
    {
        buildEngineNotAvailableError(request, response);
        co_return;
    }

    std::vector<std::string> remoteCaps;
    // Ethereum Engine API spec: params[0] is the capabilities array
    auto const& capsArray = request[0u];
    for (auto const& cap : capsArray)
    {
        remoteCaps.push_back(cap.asString());
    }

    auto caps = co_await engineService->exchangeCapabilities(std::move(remoteCaps));
    Json::Value result(Json::arrayValue);
    for (auto const& cap : caps)
    {
        result.append(cap);
    }
    buildJsonContent(result, response);
}

task::Task<void> EngineEndpoint::forkchoiceUpdatedV1(
    const Json::Value& request, Json::Value& response)
{
    auto& engineService = m_nodeService->engineService();
    if (!engineService)
    {
        buildEngineNotAvailableError(request, response);
        co_return;
    }

    auto const& params = request;
    auto forkchoiceState = parseForkchoiceState(params);
    auto payloadAttrs = parsePayloadAttributes(params);

    auto engineResult = co_await engineService->updateForkchoice(
        forkchoiceState, payloadAttrs.has_value() ? &*payloadAttrs : nullptr, 1);
    auto jsonResult = serializForkchoiceUpdatedResult(engineResult);
    buildJsonContent(jsonResult, response);
}

task::Task<void> EngineEndpoint::forkchoiceUpdatedV2(
    const Json::Value& request, Json::Value& response)
{
    auto& engineService = m_nodeService->engineService();
    if (!engineService)
    {
        buildEngineNotAvailableError(request, response);
        co_return;
    }

    auto const& params = request;
    auto forkchoiceState = parseForkchoiceState(params);
    auto payloadAttrs = parsePayloadAttributes(params);

    auto engineResult = co_await engineService->updateForkchoice(
        forkchoiceState, payloadAttrs.has_value() ? &*payloadAttrs : nullptr, 2);
    auto jsonResult = serializForkchoiceUpdatedResult(engineResult);
    buildJsonContent(jsonResult, response);
}

task::Task<void> EngineEndpoint::forkchoiceUpdatedV3(
    const Json::Value& request, Json::Value& response)
{
    auto& engineService = m_nodeService->engineService();
    if (!engineService)
    {
        buildEngineNotAvailableError(request, response);
        co_return;
    }

    auto const& params = request;
    auto forkchoiceState = parseForkchoiceState(params);
    auto payloadAttrs = parsePayloadAttributes(params);

    auto engineResult = co_await engineService->updateForkchoice(
        forkchoiceState, payloadAttrs.has_value() ? &*payloadAttrs : nullptr, 3);
    auto jsonResult = serializForkchoiceUpdatedResult(engineResult);
    buildJsonContent(jsonResult, response);
}

task::Task<void> EngineEndpoint::getPayloadV1(const Json::Value& request, Json::Value& response)
{
    auto& engineService = m_nodeService->engineService();
    if (!engineService)
    {
        buildEngineNotAvailableError(request, response);
        co_return;
    }

    auto const& params = request;
    engine::PayloadID payloadId = params[0u].asString();

    auto engineResult = co_await engineService->getPayload(payloadId, 1);
    auto jsonResult = serializeExecutionPayload(engineResult.executionPayload);
    buildJsonContent(jsonResult, response);
}

task::Task<void> EngineEndpoint::getPayloadV2(const Json::Value& request, Json::Value& response)
{
    auto& engineService = m_nodeService->engineService();
    if (!engineService)
    {
        buildEngineNotAvailableError(request, response);
        co_return;
    }

    auto const& params = request;
    engine::PayloadID payloadId = params[0u].asString();

    auto engineResult = co_await engineService->getPayload(payloadId, 2);
    auto jsonResult = serializeExecutionPayload(engineResult.executionPayload);
    jsonResult["blockValue"] = toQuantity(engineResult.blockValue);
    buildJsonContent(jsonResult, response);
}

task::Task<void> EngineEndpoint::getPayloadV3(const Json::Value& request, Json::Value& response)
{
    auto& engineService = m_nodeService->engineService();
    if (!engineService)
    {
        buildEngineNotAvailableError(request, response);
        co_return;
    }

    auto const& params = request;
    engine::PayloadID payloadId = params[0u].asString();

    auto engineResult = co_await engineService->getPayload(payloadId, 3);
    auto jsonResult = serializeExecutionPayload(engineResult.executionPayload);
    jsonResult["blockValue"] = toQuantity(engineResult.blockValue);
    if (engineResult.blobsBundle.has_value())
    {
        Json::Value bb(Json::objectValue);
        Json::Value commitments(Json::arrayValue);
        for (auto const& c : engineResult.blobsBundle->commitments)
        {
            commitments.append(toHexStringWithPrefix(c));
        }
        bb["commitments"] = std::move(commitments);
        Json::Value proofs(Json::arrayValue);
        for (auto const& p : engineResult.blobsBundle->proofs)
        {
            proofs.append(toHexStringWithPrefix(p));
        }
        bb["proofs"] = std::move(proofs);
        Json::Value blobs(Json::arrayValue);
        for (auto const& b : engineResult.blobsBundle->blobs)
        {
            blobs.append(toHexStringWithPrefix(b));
        }
        bb["blobs"] = std::move(blobs);
        jsonResult["blobsBundle"] = std::move(bb);
    }
    jsonResult["shouldOverrideBuilder"] = engineResult.shouldOverrideBuilder;
    buildJsonContent(jsonResult, response);
}

task::Task<void> EngineEndpoint::newPayloadV1(const Json::Value& request, Json::Value& response)
{
    auto& engineService = m_nodeService->engineService();
    if (!engineService)
    {
        buildEngineNotAvailableError(request, response);
        co_return;
    }

    auto const& params = request;
    auto newPayloadReq =
        parseNewPayloadRequest(params, *m_nodeService->blockFactory()->transactionFactory());

    auto engineResult = co_await engineService->newPayload(newPayloadReq, 1);
    auto jsonResult = serializePayloadStatus(engineResult);
    buildJsonContent(jsonResult, response);
}

task::Task<void> EngineEndpoint::newPayloadV2(const Json::Value& request, Json::Value& response)
{
    auto& engineService = m_nodeService->engineService();
    if (!engineService)
    {
        buildEngineNotAvailableError(request, response);
        co_return;
    }

    auto const& params = request;
    auto newPayloadReq =
        parseNewPayloadRequest(params, *m_nodeService->blockFactory()->transactionFactory());

    auto engineResult = co_await engineService->newPayload(newPayloadReq, 2);
    auto jsonResult = serializePayloadStatus(engineResult);
    buildJsonContent(jsonResult, response);
}

task::Task<void> EngineEndpoint::newPayloadV3(const Json::Value& request, Json::Value& response)
{
    auto& engineService = m_nodeService->engineService();
    if (!engineService)
    {
        buildEngineNotAvailableError(request, response);
        co_return;
    }

    auto const& params = request;
    auto newPayloadReq =
        parseNewPayloadRequest(params, *m_nodeService->blockFactory()->transactionFactory());

    auto engineResult = co_await engineService->newPayload(newPayloadReq, 3);
    auto jsonResult = serializePayloadStatus(engineResult);
    buildJsonContent(jsonResult, response);
}
