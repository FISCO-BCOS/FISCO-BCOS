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
 * @file GetPayload.h
 * @date 2026/5/21
 */

#pragma once

#include "Helper.h"
#include <bcos-rpc/openginerpc/Common.h>
#include <bcos-rpc/web3jsonrpc/utils/util.h>


namespace bcos::rpc
{
inline Json::Value serializeExecutionPayload(
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
    return ep;
}

inline void combineGetPayloadResponse(
    Json::Value& _result, bcos::engine::GetPayloadResult const& _response,
    engine::ApiVersion version)
{
    if (version == engine::ApiVersion::V1)
    {
        _result = serializeExecutionPayload(_response.executionPayload, version);
        return;
    }

    _result["executionPayload"] = serializeExecutionPayload(_response.executionPayload, version);
    _result["blockValue"] = toQuantity(_response.blockValue);

    if (version == engine::ApiVersion::V2)
    {
        return;
    }

    // V3+: blobsBundle and shouldOverrideBuilder
    Json::Value blobsBundle(Json::objectValue);
    blobsBundle["commitments"] = Json::Value(Json::arrayValue);
    blobsBundle["proofs"] = Json::Value(Json::arrayValue);
    blobsBundle["blobs"] = Json::Value(Json::arrayValue);
    if (_response.blobsBundle.has_value())
    {
        Json::Value commitments(Json::arrayValue);
        for (auto const& commitment : _response.blobsBundle->commitments)
        {
            commitments.append(toHexStringWithPrefix(commitment));
        }
        blobsBundle["commitments"] = std::move(commitments);

        Json::Value proofs(Json::arrayValue);
        for (auto const& proof : _response.blobsBundle->proofs)
        {
            proofs.append(toHexStringWithPrefix(proof));
        }
        blobsBundle["proofs"] = std::move(proofs);

        Json::Value blobs(Json::arrayValue);
        for (auto const& blob : _response.blobsBundle->blobs)
        {
            blobs.append(toHexStringWithPrefix(blob));
        }
        blobsBundle["blobs"] = std::move(blobs);
    }
    _result["blobsBundle"] = std::move(blobsBundle);
    _result["shouldOverrideBuilder"] = _response.shouldOverrideBuilder;
}
}  // namespace bcos::rpc
