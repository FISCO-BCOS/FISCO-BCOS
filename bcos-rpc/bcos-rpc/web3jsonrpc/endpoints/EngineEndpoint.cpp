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
#include <bcos-rpc/web3jsonrpc/utils/Common.h>
#include <bcos-rpc/web3jsonrpc/utils/EngineErrorMapper.h>
#include <bcos-rpc/web3jsonrpc/utils/EngineHelper.h>
#include <bcos-rpc/web3jsonrpc/utils/util.h>

using namespace bcos;
using namespace bcos::rpc;

namespace
{
/// Convert an engine-service exception into the JSON-RPC error the Engine API assigns to the
/// condition. bcos::Exception-derived engine errors previously escaped to Web3JsonRpcImpl's
/// catch(...) and came back as -32603; converting at the endpoint keeps Web3JsonRpcImpl generic.
[[noreturn]] void rethrowAsJsonRpcError(bcos::Exception const& e)
{
    // bcos::Error stores its message in errorMessage() (not what()); fall back so a
    // storage/service fault never surfaces as -32603 with an empty message.
    auto msg = dynamic_cast<bcos::Error const*>(&e);
    auto message = msg ? msg->errorMessage() : std::string(e.what());
    throw JsonRpcException(mapEngineErrorCode(e), std::move(message));
}
}  // namespace

EngineEndpoint::EngineEndpoint(NodeService::Ptr nodeService) : m_nodeService(std::move(nodeService))
{}

void EngineEndpoint::buildEngineNotAvailableError(Json::Value& response) const
{
    Json::Value error;
    error["code"] = Web3DefaultError;
    error["message"] = "Engine service is not available on this node";
    response["jsonrpc"] = "2.0";
    response["error"] = std::move(error);
}

task::Task<void> EngineEndpoint::exchangeCapabilities(
    const Json::Value& request, Json::Value& response)
{
    auto& engineService = m_nodeService->engineService();
    if (!engineService)
    {
        // The engine service may be absent (e.g. a MAX/tars node without
        // op_engine_rpc wiring). Return a clean JSON-RPC error instead of
        // dereferencing null under release builds (where assert is compiled out).
        buildEngineNotAvailableError(response);
        co_return;
    }

    std::vector<std::string> remoteCaps;
    auto const& capsArray = request[0u];
    for (auto const& cap : capsArray)
    {
        // This is the FIRST Engine method a CL calls, and asString() throws
        // Json::LogicError on an array/object element — which the RPC entry point turns
        // into -32603 carrying boost's diagnostic string back to the caller.
        if (!cap.isString())
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(
                InvalidParams, "engine_exchangeCapabilities expects an array of method names"));
        }
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

void EngineEndpoint::buildUnimplementedVersionError(
    std::string_view method, Json::Value& response) const
{
    // -38005 Unsupported fork for a method version this node does not implement at all
    // (currently only forkchoiceUpdatedV4: the service layer's forkchoice window tops out
    // at V3, see isForkchoiceVersionSupported). This is NOT a declaration that older
    // versions are incompatible: every version that IS implemented stays served, so a
    // pre-Karst CL — the v1 Engine API harness kept alive by unsafe_allow_v1_executor, or
    // a stock Lodestar driving V1-V3 — keeps working.
    //
    // Built inline instead of through buildJsonError(request, ...) because a handler only
    // ever receives the params array, never the request envelope: the JSON-RPC id is
    // stamped onto the handler's response by the dispatcher
    // (Web3JsonRpcImpl::handleRequest, `result["id"] = _request["id"]`), the same way it
    // is for buildEngineNotAvailableError above and for every successful response.
    Json::Value error(Json::objectValue);
    error["code"] = EngineError::UnsupportedFork;
    error["message"] = std::string(method) + " is not yet supported";
    response["jsonrpc"] = "2.0";
    response["error"] = std::move(error);
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
    const Json::Value& request, Json::Value& response)
{
    // Tier-2 (08-19): the OP face is Isthmus+/V4-only — the engine's attribute-driven build and
    // the newPayload path both gate on V4, so the RPC surface serves it (Prague payload shape;
    // executionRequests stays empty on this chain).
    co_await handleForkchoiceUpdated(engine::ApiVersion::V4, request, response);
}

task::Task<void> EngineEndpoint::handleForkchoiceUpdated(
    engine::ApiVersion version, const Json::Value& request, Json::Value& response)
{
    auto& engineService = m_nodeService->engineService();
    if (!engineService)
    {
        buildEngineNotAvailableError(response);
        co_return;
    }

    auto forkchoiceState = parseForkchoiceState(request);
    auto payloadAttrs = parsePayloadAttributes(request, version);
    bcos::engine::ForkchoiceUpdatedResult engineResult;
    try
    {
        engineResult = co_await engineService->updateForkchoice(forkchoiceState,
            payloadAttrs.has_value() ? &*payloadAttrs : nullptr, static_cast<uint32_t>(version));
    }
    catch (bcos::Exception const& e)
    {
        rethrowAsJsonRpcError(e);
    }
    auto jsonResult = combineForkchoiceUpdatedResult(engineResult, version);
    buildJsonContent(jsonResult, response);
}

task::Task<void> EngineEndpoint::getPayloadV1(const Json::Value& request, Json::Value& response)
{
    co_await handleGetPayload(engine::ApiVersion::V1, request, response);
}

task::Task<void> EngineEndpoint::getPayloadV2(const Json::Value& request, Json::Value& response)
{
    co_await handleGetPayload(engine::ApiVersion::V2, request, response);
}

task::Task<void> EngineEndpoint::getPayloadV3(const Json::Value& request, Json::Value& response)
{
    co_await handleGetPayload(engine::ApiVersion::V3, request, response);
}

task::Task<void> EngineEndpoint::getPayloadV4(const Json::Value& request, Json::Value& response)
{
    // Isthmus getPayload — a pre-Karst version, and served like every other one. The whole
    // stack below already handles V4: isGetPayloadVersionSupported spans V1-V5,
    // isGetPayloadVersionCompatible has its own V4 window, handleGetPayload fills
    // executionRequests for V4+, and combineGetPayloadResponse renders the V4 shape.
    // Refusing it here while serving newPayloadV4 would be the same fork-narrowing this
    // node no longer does.
    co_await handleGetPayload(engine::ApiVersion::V4, request, response);
}

task::Task<void> EngineEndpoint::getPayloadV5(const Json::Value& request, Json::Value& response)
{
    co_await handleGetPayload(engine::ApiVersion::V5, request, response);
}

task::Task<void> EngineEndpoint::handleGetPayload(
    engine::ApiVersion version, const Json::Value& request, Json::Value& response)
{
    auto& engineService = m_nodeService->engineService();
    if (!engineService)
    {
        buildEngineNotAvailableError(response);
        co_return;
    }

    if (request.size() < 1 || !request[0u].isString())
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(InvalidParams, "engine_getPayload expects [payloadId]"));
    }
    engine::PayloadID payloadId = request[0u].asString();
    bcos::engine::GetPayloadResult engineResult;
    try
    {
        engineResult =
            co_await engineService->getPayload(payloadId, static_cast<uint32_t>(version));
    }
    catch (bcos::Exception const& e)
    {
        rethrowAsJsonRpcError(e);
    }
    if (!engineResult)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(EngineError::UnknownPayload,
            "Unknown payload: no build process identified by the given payloadId"));
    }

    Json::Value result;
    combineGetPayloadResponse(result, engineResult, version);
    buildJsonContent(result, response);
}

task::Task<void> EngineEndpoint::newPayloadV1(const Json::Value& request, Json::Value& response)
{
    co_await handleNewPayload(engine::ApiVersion::V1, request, response);
}

task::Task<void> EngineEndpoint::newPayloadV2(const Json::Value& request, Json::Value& response)
{
    co_await handleNewPayload(engine::ApiVersion::V2, request, response);
}

task::Task<void> EngineEndpoint::newPayloadV3(const Json::Value& request, Json::Value& response)
{
    co_await handleNewPayload(engine::ApiVersion::V3, request, response);
}

task::Task<void> EngineEndpoint::newPayloadV4(const Json::Value& request, Json::Value& response)
{
    co_await handleNewPayload(engine::ApiVersion::V4, request, response);
}

task::Task<void> EngineEndpoint::handleNewPayload(
    engine::ApiVersion version, const Json::Value& request, Json::Value& response)
{
    auto& engineService = m_nodeService->engineService();
    if (!engineService)
    {
        buildEngineNotAvailableError(response);
        co_return;
    }

    auto newPayloadReq = parseNewPayloadRequest(request, version);
    bcos::engine::PayloadStatus engineResult;
    try
    {
        engineResult =
            co_await engineService->newPayload(newPayloadReq, static_cast<uint32_t>(version));
    }
    catch (bcos::Exception const& e)
    {
        rethrowAsJsonRpcError(e);
    }
    auto result = serializePayloadStatus(engineResult, version);
    buildJsonContent(result, response);
}
