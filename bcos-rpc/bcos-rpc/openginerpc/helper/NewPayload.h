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
 * @file NewPayload.h
 * @date 2026/5/21
 */

#pragma once

#include <bcos-rpc/openginerpc/Common.h>
#include <bcos-rpc/web3jsonrpc/utils/util.h>
#include <bcos-utilities/DataConvertUtility.h>
#include "Helper.h"


namespace bcos::rpc
{
inline bcos::engine::NewPayloadRequest parseNewPayloadRequest(
    Json::Value const& params, bcos::protocol::TransactionFactory& transactionFactory,
    engine::ApiVersion version)
{
    auto const& ep = params[0u];
    bcos::engine::ExecutionPayload payload{
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
        .blockAccessList = std::nullopt,
        .slotNumber = std::nullopt,
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
            BOOST_THROW_EXCEPTION(JsonRpcException(
                InvalidParams, "Expected 256-byte hex string for logsBloom, got " +
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
                .address = parseAddress(w["address"].asString()),
                .amount = fromBigQuantity(w["amount"].asString()),
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

    bcos::engine::NewPayloadRequest request{
        .executionPayload = std::move(payload),
        .expectedBlobVersionedHashes = {},
        .parentBeaconBlockRoot = std::nullopt,
        .executionRequests = std::nullopt,
    };
    if (version >= engine::ApiVersion::V3 &&
        params.size() >= 2 && params[1].isArray())
    {
        for (auto const& h : params[1])
        {
            request.expectedBlobVersionedHashes.push_back(parseH256(h.asString()));
        }
    }
    if (version >= engine::ApiVersion::V3 &&
        params.size() >= 3 && !params[2].isNull())
    {
        request.parentBeaconBlockRoot = parseH256(params[2].asString());
    }
    return request;
}

inline Json::Value serializePayloadStatus(
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
        result["status"] =
            (version >= engine::ApiVersion::V3) ?
                "INVALID" :
                "INVALID_BLOCK_HASH";
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

inline void combineNewPayloadResponse(
    Json::Value& _result, bcos::engine::PayloadStatus const& _response, engine::ApiVersion version)
{
    _result = serializePayloadStatus(_response, version);
}
}  // namespace bcos::rpc
