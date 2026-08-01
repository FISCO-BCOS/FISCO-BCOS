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
#include <bcos-rpc/web3jsonrpc/utils/EngineHelper.h>
#include <bcos-rpc/web3jsonrpc/utils/Common.h>
#include <bcos-rpc/web3jsonrpc/utils/util.h>
#include <bcos-utilities/DataConvertUtility.h>

using namespace bcos;
using namespace bcos::rpc;


EngineEndpoint::EngineEndpoint(NodeService::Ptr nodeService) : m_nodeService(std::move(nodeService))
{}

task::Task<void> EngineEndpoint::exchangeCapabilities(
    const Json::Value& request, Json::Value& response)
{
    auto& engineService = m_nodeService->engineService();
    assert(engineService && "engineService is not available");

    std::vector<std::string> remoteCaps;
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
    co_await handleForkchoiceUpdated(engine::ApiVersion::V1, request, response);
}

task::Task<void> EngineEndpoint::forkchoiceUpdatedV2(
    const Json::Value& request, Json::Value& response)
{
    co_await handleForkchoiceUpdated(engine::ApiVersion::V2, request, response);
}

task::Task<void> EngineEndpoint::forkchoiceUpdatedV3(
    const Json::Value& request, Json::Value& response)
{
    co_await handleForkchoiceUpdated(engine::ApiVersion::V3, request, response);
}

task::Task<void> EngineEndpoint::forkchoiceUpdatedV4(
    const Json::Value&, Json::Value& response)
{
    // V4 not yet implemented (Prague fork)
    Json::Value request;
    buildJsonError(request, EngineError::UnsupportedFork,
        "engine_forkchoiceUpdatedV4 is not yet supported", response);
    co_return;
}

task::Task<void> EngineEndpoint::handleForkchoiceUpdated(
    engine::ApiVersion version, const Json::Value& request, Json::Value& response)
{
    auto& engineService = m_nodeService->engineService();
    assert(engineService && "engineService is not available");

    auto forkchoiceState = parseForkchoiceState(request);
    auto payloadAttrs = parsePayloadAttributes(request, version);
    // TODO: engineService->updateForkchoice() MUST throw JsonRpcException in these cases:
    //   -38002 InvalidForkchoiceState: headBlockHash is VALID but finalizedBlockHash/safeBlockHash not in chain
    //   -38003 InvalidPayloadAttributes: payloadAttributes.timestamp <= headBlockHash.timestamp
    //   -38005 UnsupportedFork: timestamp out of fork window (V2/V3 specific)
    //   -38006 TooDeepReorg: reorg depth exceeds limitation
    auto engineResult = co_await engineService->updateForkchoice(
        forkchoiceState, payloadAttrs.has_value() ? &*payloadAttrs : nullptr,
        static_cast<uint32_t>(version));
    auto jsonResult = combineForkchoiceUpdatedResult(engineResult, version);
    buildJsonContent(jsonResult, response);
}

task::Task<void> EngineEndpoint::getPayloadV1(
    const Json::Value& request, Json::Value& response)
{
    co_await handleGetPayload(engine::ApiVersion::V1, request, response);
}

task::Task<void> EngineEndpoint::getPayloadV2(
    const Json::Value& request, Json::Value& response)
{
    co_await handleGetPayload(engine::ApiVersion::V2, request, response);
}

task::Task<void> EngineEndpoint::getPayloadV3(
    const Json::Value& request, Json::Value& response)
{
    co_await handleGetPayload(engine::ApiVersion::V3, request, response);
}

task::Task<void> EngineEndpoint::getPayloadV4(
    const Json::Value&, Json::Value& response)
{
    // V4 not yet implemented (Prague fork)
    Json::Value request;
    buildJsonError(request, EngineError::UnsupportedFork,
        "engine_getPayloadV4 is not yet supported", response);
    co_return;
}

task::Task<void> EngineEndpoint::handleGetPayload(
    engine::ApiVersion version, const Json::Value& request, Json::Value& response)
{
    auto& engineService = m_nodeService->engineService();
    assert(engineService && "engineService is not available");

    engine::PayloadID payloadId = request[0u].asString();
    auto engineResult = co_await engineService->getPayload(
        payloadId, static_cast<uint32_t>(version));
    if (!engineResult)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(EngineError::UnknownPayload,
            "Unknown payload: no build process identified by the given payloadId"));
    }

    Json::Value result;
    combineGetPayloadResponse(result, engineResult, version);
    buildJsonContent(result, response);
}

task::Task<void> EngineEndpoint::newPayloadV1(
    const Json::Value& request, Json::Value& response)
{
    co_await handleNewPayload(engine::ApiVersion::V1, request, response);
}

task::Task<void> EngineEndpoint::newPayloadV2(
    const Json::Value& request, Json::Value& response)
{
    co_await handleNewPayload(engine::ApiVersion::V2, request, response);
}

task::Task<void> EngineEndpoint::newPayloadV3(
    const Json::Value& request, Json::Value& response)
{
    co_await handleNewPayload(engine::ApiVersion::V3, request, response);
}

task::Task<void> EngineEndpoint::newPayloadV4(
    const Json::Value&, Json::Value& response)
{
    // V4 not yet implemented (Prague fork)
    Json::Value request;
    buildJsonError(request, EngineError::UnsupportedFork,
        "engine_newPayloadV4 is not yet supported", response);
    co_return;
}

task::Task<void> EngineEndpoint::handleNewPayload(
    engine::ApiVersion version, const Json::Value& request, Json::Value& response)
{
    auto& engineService = m_nodeService->engineService();
    assert(engineService && "engineService is not available");

    auto newPayloadReq =
        parseNewPayloadRequest(request, *m_nodeService->blockFactory()->transactionFactory(), version);
    // TODO: engineService->newPayload() MUST throw JsonRpcException in these cases:
    //   -32602 InvalidParams: wrong version of ExecutionPayload structure (V2)
    //   -38005 UnsupportedFork: timestamp out of fork window (V2/V3)
    auto engineResult = co_await engineService->newPayload(
        newPayloadReq, static_cast<uint32_t>(version));
    auto result = serializePayloadStatus(engineResult, version);
    buildJsonContent(result, response);
}
