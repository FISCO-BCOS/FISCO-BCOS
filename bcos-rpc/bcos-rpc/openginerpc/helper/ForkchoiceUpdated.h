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
    auto const& param = params[1];
    bcos::engine::PayloadAttributes payloadAttribute{
        .timestamp = fromQuantity(param["timestamp"].asString()),
        .prevRandao = parseH256(param["prevRandao"].asString()),
        .suggestedFeeRecipient = parseAddress(param["suggestedFeeRecipient"].asString()),
        .withdrawals = std::nullopt,
        .parentBeaconBlockRoot = std::nullopt,
        .slotNumber = std::nullopt,
        .targetGasLimit = std::nullopt,
    };
    if (version >= engine::ApiVersion::V2 && !param.isMember("withdrawals")) {
        // TODO: need to return -32602: Invalid params;
        // TODO： need a check function；
    }
    if(!param["withdrawals"].isNull())
    {
        std::vector<bcos::engine::WithdrawalV1> withdrawals;
        for (auto const& withdrawal : param["withdrawals"])
        {
            withdrawals.push_back(bcos::engine::WithdrawalV1{
                .index = fromBigQuantity(withdrawal["index"].asString()),
                .validatorIndex = fromBigQuantity(withdrawal["validatorIndex"].asString()),
                .address = parseAddress(withdrawal["address"].asString()),
                .amount = fromBigQuantity(withdrawal["amount"].asString()),
            });
        }
        payloadAttribute.withdrawals = std::move(withdrawals);
    }

    if (param.isMember("parentBeaconBlockRoot") && !param["parentBeaconBlockRoot"].isNull())
    {
        payloadAttribute.parentBeaconBlockRoot = parseH256(param["parentBeaconBlockRoot"].asString());
    }

    if (version >= engine::ApiVersion::V4 && param.isMember("slotNumber") && !param["slotNumber"].isNull())
    {
        payloadAttribute.slotNumber = fromQuantity(std::string(param["slotNumber"].asString()));
    }

    if (version >= engine::ApiVersion::V4 && param.isMember("targetGasLimit") && !param["targetGasLimit"].isNull())
    {
        payloadAttribute.targetGasLimit = fromQuantity(std::string(param["targetGasLimit"].asString()));
    }
    return payloadAttribute;
}

inline bcos::engine::ForkchoiceState parseForkchoiceState(Json::Value const& params)
{
    auto const& param = params[0];
    return bcos::engine::ForkchoiceState{
        .headBlockHash = parseH256(param["headBlockHash"].asString()),
        .safeBlockHash = parseH256(param["safeBlockHash"].asString()),
        .finalizedBlockHash = parseH256(param["finalizedBlockHash"].asString()),
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
