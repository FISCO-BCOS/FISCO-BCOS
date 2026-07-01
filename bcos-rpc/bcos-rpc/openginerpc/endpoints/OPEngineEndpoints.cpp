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
 * @file OPEngineEndpoints.cpp
 * @date 2026/5/20
 */

#include "OPEngineEndpoints.h"

using namespace bcos;
using namespace bcos::rpc;

namespace
{
constexpr int32_t kEngineNotAvailable = -32000;

void buildEngineNotAvailableError()
{
    BOOST_THROW_EXCEPTION(
        JsonRpcException(kEngineNotAvailable, "Engine service is not available on this node"));
}
}  // namespace

namespace bcos::rpc
{
task::Task<Json::Value> OPEngineEndpoints::exchangeCapabilities(const Json::Value& _request)
{
    if (!m_engineService)
    {
        buildEngineNotAvailableError();
    }

    std::vector<std::string> remoteCaps;
    remoteCaps.reserve(_request.size());
    for (auto const& cap : _request)
    {
        if (!cap.isString())
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(
                InvalidParams, "engine_exchangeCapabilities expects string capability items"));
        }
        remoteCaps.emplace_back(cap.asString());
    }

    auto caps = co_await m_engineService->exchangeCapabilities(std::move(remoteCaps));
    Json::Value result(Json::arrayValue);
    for (auto const& cap : caps)
    {
        result.append(cap);
    }
    co_return result;
}

task::Task<Json::Value> OPEngineEndpoints::forkchoiceUpdatedV1(const Json::Value& _request)
{
    co_return co_await handleForkchoiceUpdated(engine::ApiVersion::V1, _request);
}

task::Task<Json::Value> OPEngineEndpoints::forkchoiceUpdatedV2(const Json::Value& _request)
{
    co_return co_await handleForkchoiceUpdated(engine::ApiVersion::V2, _request);
}

task::Task<Json::Value> OPEngineEndpoints::forkchoiceUpdatedV3(const Json::Value& _request)
{
    co_return co_await handleForkchoiceUpdated(engine::ApiVersion::V3, _request);
}

task::Task<Json::Value> OPEngineEndpoints::forkchoiceUpdatedV4(const Json::Value& _request)
{
    // V4 not yet implemented (Prague fork)
    BOOST_THROW_EXCEPTION(
        JsonRpcException(EngineError::UnsupportedFork, "engine_forkchoiceUpdatedV4 is not yet supported"));
    co_return {};
}

task::Task<Json::Value> OPEngineEndpoints::getPayloadV1(const Json::Value& _request)
{
    co_return co_await handleGetPayload(engine::ApiVersion::V1, _request);
}

task::Task<Json::Value> OPEngineEndpoints::getPayloadV2(const Json::Value& _request)
{
    co_return co_await handleGetPayload(engine::ApiVersion::V2, _request);
}

task::Task<Json::Value> OPEngineEndpoints::getPayloadV3(const Json::Value& _request)
{
    co_return co_await handleGetPayload(engine::ApiVersion::V3, _request);
}

task::Task<Json::Value> OPEngineEndpoints::getPayloadV4(const Json::Value& _request)
{
    // V4 not yet implemented (Prague fork)
    BOOST_THROW_EXCEPTION(
        JsonRpcException(EngineError::UnsupportedFork, "engine_getPayloadV4 is not yet supported"));
    co_return {};
}

task::Task<Json::Value> OPEngineEndpoints::newPayloadV1(const Json::Value& _request)
{
    co_return co_await handleNewPayload(engine::ApiVersion::V1, _request);
}

task::Task<Json::Value> OPEngineEndpoints::newPayloadV2(const Json::Value& _request)
{
    co_return co_await handleNewPayload(engine::ApiVersion::V2, _request);
}

task::Task<Json::Value> OPEngineEndpoints::newPayloadV3(const Json::Value& _request)
{
    co_return co_await handleNewPayload(engine::ApiVersion::V3, _request);
}

task::Task<Json::Value> OPEngineEndpoints::newPayloadV4(const Json::Value& _request)
{
    // V4 not yet implemented (Prague fork)
    BOOST_THROW_EXCEPTION(
        JsonRpcException(EngineError::UnsupportedFork, "engine_newPayloadV4 is not yet supported"));
    co_return {};
}

task::Task<Json::Value> OPEngineEndpoints::handleForkchoiceUpdated(engine::ApiVersion version, const Json::Value& _request)
{
    if (!m_engineService)
    {
        buildEngineNotAvailableError();
    }

    auto forkchoiceState = parseForkchoiceState(_request);
    auto payloadAttrs = parsePayloadAttributes(_request, version);
    // TODO: engineService->updateForkchoice() MUST throw JsonRpcException in these cases:
    //   -38002 InvalidForkchoiceState: headBlockHash is VALID but finalizedBlockHash/safeBlockHash not in chain
    //   -38003 InvalidPayloadAttributes: payloadAttributes.timestamp <= headBlockHash.timestamp
    //   -38005 UnsupportedFork: timestamp out of fork window (V2/V3 specific)
    //   -38006 TooDeepReorg: reorg depth exceeds limitation
    auto engineResult = co_await m_engineService->updateForkchoice(
        forkchoiceState, payloadAttrs.has_value() ? &*payloadAttrs : nullptr,
        toServiceVersion(version));

    co_return combineForkchoiceUpdatedResult(engineResult, version);
}

task::Task<Json::Value> OPEngineEndpoints::handleGetPayload(engine::ApiVersion version, const Json::Value& _request)
{
    if (!m_engineService)
    {
        buildEngineNotAvailableError();
    }
    if (!_request.isArray() || _request.size() != 1 || !_request[0u].isString())
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(InvalidParams, "Invalid params for engine_getPayload"));
    }

    auto engineResult =
        co_await m_engineService->getPayload(_request[0u].asString(), toServiceVersion(version));
    // TODO: engineService->getPayload() MUST throw JsonRpcException in these cases:
    //   -38001 UnknownPayload: build process identified by payloadId does not exist
    //   -38005 UnsupportedFork: timestamp of built payload out of fork window (V2/V3)

    Json::Value result(Json::objectValue);
    combineGetPayloadResponse(result, engineResult, version);
    co_return result;
}

task::Task<Json::Value> OPEngineEndpoints::handleNewPayload(engine::ApiVersion version, const Json::Value& _request)
{
    if (!m_engineService)
    {
        buildEngineNotAvailableError();
    }

    auto engineRequest =
        parseNewPayloadRequest(_request, *m_nodeService->blockFactory()->transactionFactory(), version);
    // TODO: engineService->newPayload() MUST throw JsonRpcException in these cases:
    //   -32602 InvalidParams: wrong version of ExecutionPayload structure (V2)
    //   -38005 UnsupportedFork: timestamp out of fork window (V2/V3)
    auto engineResult =
        co_await m_engineService->newPayload(engineRequest, toServiceVersion(version));
    co_return serializePayloadStatus(engineResult, version);
}

std::uint32_t OPEngineEndpoints::toServiceVersion(engine::ApiVersion version)
{
    return static_cast<uint32_t>(version);
}
}  // namespace bcos::rpc
