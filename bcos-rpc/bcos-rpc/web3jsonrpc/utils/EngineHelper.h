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
 * @file Helper.h
 * @date 2026/5/21
 */

#pragma once

#include <algorithm>
#include <bcos-framework/engine/Types.h>
#include <bcos-rpc/jsonrpc/Common.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <json/json.h>
#include <string>

namespace bcos::rpc
{
/// @brief Parse a hex-encoded 32-byte h256 value.
///        Kept inline in the header because it is small and on hot paths.
inline bcos::h256 parseH256(std::string_view hex)
{
    auto bytes = fromHex(hex);
    if (bytes.size() != 32)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(
            InvalidParams, "Expected 32-byte hex string for h256, got " +
                               std::to_string(bytes.size()) + " bytes"));
    }
    h256 result;
    std::ranges::copy(bytes.begin(), bytes.end(), result.begin());
    return result;
}

/// @brief Parse a hex-encoded 20-byte address.
///        Kept inline in the header because it is small and on hot paths.
inline bcos::Address parseAddress(std::string_view hex)
{
    auto bytes = fromHex(hex);
    if (bytes.size() != 20)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(
            InvalidParams, "Expected 20-byte hex string for address, got " +
                               std::to_string(bytes.size()) + " bytes"));
    }
    Address result;
    std::ranges::copy(bytes.begin(), bytes.end(), result.begin());
    return result;
}

// The functions below are defined in EngineHelper.cpp to avoid pulling
// ~300 lines of serialization code and <TransactionFactory.h> into every
// translation unit that transitively includes this header.

bcos::engine::NewPayloadRequest parseNewPayloadRequest(
    Json::Value const& params, bcos::protocol::TransactionFactory& transactionFactory,
    engine::ApiVersion version);

Json::Value serializePayloadStatus(
    bcos::engine::PayloadStatus const& status, engine::ApiVersion version);

void combineNewPayloadResponse(Json::Value& _result,
    bcos::engine::PayloadStatus const& _response, engine::ApiVersion version);

std::optional<bcos::engine::PayloadAttributes> parsePayloadAttributes(
    Json::Value const& params, engine::ApiVersion version);

bcos::engine::ForkchoiceState parseForkchoiceState(Json::Value const& params);

Json::Value combineForkchoiceUpdatedResult(
    bcos::engine::ForkchoiceUpdatedResult const& result, engine::ApiVersion version);

Json::Value serializeExecutionPayload(
    bcos::engine::ExecutionPayload const& payload, engine::ApiVersion version);

void combineGetPayloadResponse(Json::Value& _result,
    bcos::engine::GetPayloadResult const& _response, engine::ApiVersion version);

}  // namespace bcos::rpc
