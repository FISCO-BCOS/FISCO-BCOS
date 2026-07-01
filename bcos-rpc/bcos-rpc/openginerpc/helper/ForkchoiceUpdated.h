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
 * @file ForkchoiceUpdated.h
 * @date 2026/5/21
 */

#pragma once

#include "NewPayload.h"
#include "Helper.h"
#include <bcos-rpc/web3jsonrpc/utils/util.h>
#include <bcos-utilities/DataConvertUtility.h>

namespace bcos::rpc
{
inline std::optional<bcos::engine::PayloadAttributes> parsePayloadAttributes(
    Json::Value const& params, engine::ApiVersion version)
{
    if (params.size() < 2 || params[1].isNull())
    {
        return std::nullopt;
    }
    auto const& pa = params[1];
    bcos::engine::PayloadAttributes attrs{
        .timestamp = fromQuantity(std::string(pa["timestamp"].asString())),
        .prevRandao = parseH256(pa["prevRandao"].asString()),
        .suggestedFeeRecipient = parseAddress(pa["suggestedFeeRecipient"].asString()),
        .withdrawals = std::nullopt,
        .parentBeaconBlockRoot = std::nullopt,
        .slotNumber = std::nullopt,
        .targetGasLimit = std::nullopt,
    };
    if (pa.isMember("withdrawals") && !pa["withdrawals"].isNull())
    {
        std::vector<bcos::engine::WithdrawalV1> withdrawals;
        for (auto const& w : pa["withdrawals"])
        {
            withdrawals.push_back(bcos::engine::WithdrawalV1{
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

inline bcos::engine::ForkchoiceState parseForkchoiceState(Json::Value const& params)
{
    auto const& fc = params[0u];
    return bcos::engine::ForkchoiceState{
        .headBlockHash = parseH256(fc["headBlockHash"].asString()),
        .safeBlockHash = parseH256(fc["safeBlockHash"].asString()),
        .finalizedBlockHash = parseH256(fc["finalizedBlockHash"].asString()),
    };
}

inline Json::Value combineForkchoiceUpdatedResult(
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
}  // namespace bcos::rpc
