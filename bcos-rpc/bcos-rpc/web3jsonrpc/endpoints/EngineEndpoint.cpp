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
#include <bcos-framework/engine/Errors.h>
#include <bcos-rpc/jsonrpc/Common.h>
#include <bcos-rpc/web3jsonrpc/utils/Common.h>
#include <bcos-rpc/web3jsonrpc/utils/EngineHelper.h>
#include <bcos-rpc/web3jsonrpc/utils/util.h>
#include <exception>

using namespace bcos;
using namespace bcos::rpc;

namespace
{
/// Clears the OP newPayload in-flight flag, including on exception unwind.
struct OpPayloadBusyReset
{
    std::atomic<bool>& flag;
    bool owned;
    OpPayloadBusyReset(std::atomic<bool>& busy, bool owns) : flag(busy), owned(owns) {}
    OpPayloadBusyReset(OpPayloadBusyReset const&) = delete;
    OpPayloadBusyReset& operator=(OpPayloadBusyReset const&) = delete;
    OpPayloadBusyReset(OpPayloadBusyReset&&) = delete;
    OpPayloadBusyReset& operator=(OpPayloadBusyReset&&) = delete;
    ~OpPayloadBusyReset()
    {
        if (owned)
        {
            flag.store(false, std::memory_order_release);
        }
    }
};

/// Map remaining typed / untyped service faults to JSON-RPC -32603 with a short
/// stable message. Must not use boost::diagnostic_information (finding AM).
/// OpExecutionInternalError without OpPayloadUndecodable stays -32603, never INVALID.
[[noreturn]] void rethrowAsEngineInternalError(std::exception const& e)
{
    auto const* what = e.what();
    std::string message = "Internal error";
    if (what != nullptr && what[0] != '\0')
    {
        message += ": ";
        message += what;
    }
    BOOST_THROW_EXCEPTION(JsonRpcException(InternalError, std::move(message)));
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
    // -38005: this method version is not implemented. The dispatcher stamps JSON-RPC id.
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

task::Task<void> EngineEndpoint::forkchoiceUpdatedV4(const Json::Value&, Json::Value& response)
{
    // Prague forkchoiceUpdated shape; not implemented (Karst builds payloads on V3).
    buildUnimplementedVersionError("engine_forkchoiceUpdatedV4", response);
    co_return;
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
    // Map typed engine failures to spec codes; other exceptions stay -32603.

    engine::ForkchoiceUpdatedResult engineResult;
    try
    {
        engineResult = co_await engineService->updateForkchoice(forkchoiceState,
            payloadAttrs.has_value() ? &*payloadAttrs : nullptr, static_cast<uint32_t>(version));
    }
    catch (engine::InvalidPayloadAttributes const& e)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(EngineError::InvalidPayloadAttributes,
            std::string("Invalid payload attributes: ") + e.what()));
    }
    catch (engine::UnsupportedFork const& e)
    {
        // The request's attribute shape cannot express the chain's fork era, or the chain
        // lacks an on-chain EVM revision entirely. geth answers -38005 Unsupported fork
        // for the same CL/chain mismatch; the service layer throws UnsupportedFork so this
        // stays a diagnosable fork error instead of a generic -32603 InternalError. Keep
        // the exception's errinfo_comment so the operator can tell which gate fired — the
        // missing-revision case is a NODE-side misconfiguration and the generic shape
        // message would wrongly point at the CL.
        BOOST_THROW_EXCEPTION(JsonRpcException(
            EngineError::UnsupportedFork, std::string("Unsupported fork: ") + e.what()));
    }
    catch (engine::UnsupportedEngineApiVersion const& e)
    {
        // Service-layer method-version reject (finding AM). Same -38005 as UnsupportedFork
        // and the FCU V4 endpoint stub; do not let this fall through to -32603.
        BOOST_THROW_EXCEPTION(JsonRpcException(
            EngineError::UnsupportedFork, std::string("Unsupported fork: ") + e.what()));
    }
    catch (engine::InvalidForkchoiceState const& e)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(EngineError::InvalidForkchoiceState,
            std::string("Invalid forkchoice state: ") + e.what()));
    }
    catch (engine::OpExecutionInternalError const& e)
    {
        rethrowAsEngineInternalError(e);
    }
    catch (std::exception const& e)
    {
        rethrowAsEngineInternalError(e);
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
    engine::GetPayloadResult engineResult;
    try
    {
        engineResult =
            co_await engineService->getPayload(payloadId, static_cast<uint32_t>(version));
    }
    catch (engine::UnknownPayload const&)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(EngineError::UnknownPayload,
            "Unknown payload: no build process identified by the given payloadId"));
    }
    catch (engine::IncompatiblePayloadVersion const&)
    {
        // The build behind this payloadId is outside the requested method's version
        // window (forkchoiceUpdated version vs getPayload version). newPayload keeps the
        // FCU-built version tag. The mapping is -38005 to match op-geth, whose getPayload
        // helper answers engine.UnsupportedFork when the payloadId's encoded build version
        // is outside the method's allowed set (eth/catalyst/api.go:531-533, GetPayloadV5
        // allowing only PayloadV3).
        BOOST_THROW_EXCEPTION(JsonRpcException(EngineError::UnsupportedFork,
            "Unsupported fork: payload was built by a different method version"));
    }
    catch (engine::UnsupportedEngineApiVersion const& e)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(
            EngineError::UnsupportedFork, std::string("Unsupported fork: ") + e.what()));
    }
    catch (engine::OpExecutionInternalError const& e)
    {
        rethrowAsEngineInternalError(e);
    }
    catch (std::exception const& e)
    {
        rethrowAsEngineInternalError(e);
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

    // Parse before taking the latch so a malformed request still answers InvalidParams
    // (-32602) while a V4 payload is in flight; the latch below only bounds execution.
    auto newPayloadReq = parseNewPayloadRequest(request, version);

    // One in-flight V4 newPayload; a second concurrent call answers SYNCING.
    const bool opExecution = version == engine::ApiVersion::V4;
    if (opExecution && m_opPayloadBusy.exchange(true, std::memory_order_acq_rel))
    {
        auto syncingStatus = serializePayloadStatus(
            engine::PayloadStatus{
                .latestValidHash = std::nullopt,
                .validationError = std::nullopt,
                .status = engine::PayloadValidationStatus::Syncing,
            },
            version);
        buildJsonContent(syncingStatus, response);
        co_return;
    }
    OpPayloadBusyReset busyReset{m_opPayloadBusy, opExecution};

    engine::PayloadStatus engineResult;
    try
    {
        engineResult =
            co_await engineService->newPayload(newPayloadReq, static_cast<uint32_t>(version));
    }
    catch (engine::UnsupportedFork const& e)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(
            EngineError::UnsupportedFork, std::string("Unsupported fork: ") + e.what()));
    }
    catch (engine::UnsupportedEngineApiVersion const& e)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(
            EngineError::UnsupportedFork, std::string("Unsupported fork: ") + e.what()));
    }
    catch (engine::InvalidPayloadAttributes const& e)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(EngineError::InvalidPayloadAttributes,
            std::string("Invalid payload attributes: ") + e.what()));
    }
    catch (engine::OpExecutionInternalError const& e)
    {
        rethrowAsEngineInternalError(e);
    }
    catch (std::exception const& e)
    {
        rethrowAsEngineInternalError(e);
    }
    auto result = serializePayloadStatus(engineResult, version);
    buildJsonContent(result, response);
}
