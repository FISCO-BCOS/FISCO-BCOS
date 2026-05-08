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

using namespace bcos;
using namespace bcos::rpc;

namespace
{
void requireArray(Json::Value const& request)
{
    if (!request.isArray())
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "The params field must be an array"));
    }
}

void requireParamSize(Json::Value const& request, Json::ArrayIndex minSize, Json::ArrayIndex maxSize)
{
    if (request.size() < minSize || request.size() > maxSize)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Invalid params size"));
    }
}

std::string requireString(Json::Value const& value, std::string_view fieldName)
{
    if (!value.isString())
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(InvalidParams, std::string(fieldName) + " must be a string"));
    }
    return value.asString();
}

void requireObject(Json::Value const& value, std::string_view fieldName)
{
    if (!value.isObject())
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(InvalidParams, std::string(fieldName) + " must be an object"));
    }
}

std::string requireStringField(Json::Value const& value, std::string_view fieldName)
{
    auto fieldNameString = std::string(fieldName);
    if (!value.isMember(fieldNameString))
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(InvalidParams, std::string(fieldName) + " is required"));
    }
    return requireString(value[fieldNameString], fieldName);
}

std::vector<std::string> parseCapabilities(Json::Value const& request)
{
    requireArray(request);
    auto const& params = request;
    requireParamSize(params, 1, 1);

    auto const& capabilities = params[0U];
    if (!capabilities.isArray())
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(InvalidParams, "capabilities must be an array of strings"));
    }

    std::vector<std::string> result;
    result.reserve(capabilities.size());
    for (auto const& capability : capabilities)
    {
        result.push_back(requireString(capability, "capability"));
    }
    return result;
}

void parseForkchoiceState(Json::Value const& request)
{
    requireObject(request[0U], "forkchoiceState");
    auto const& forkchoiceState = request[0U];
    requireStringField(forkchoiceState, "headBlockHash");
    requireStringField(forkchoiceState, "safeBlockHash");
    requireStringField(forkchoiceState, "finalizedBlockHash");
}

void parsePayloadAttributes(Json::Value const& request)
{
    if (request.size() < 2 || request[1U].isNull())
    {
        return;
    }

    requireObject(request[1U], "payloadAttributes");
    auto const& payloadAttributes = request[1U];
    requireStringField(payloadAttributes, "timestamp");
}

void parseGetPayloadRequest(Json::Value const& request)
{
    requireArray(request);
    auto const& params = request;
    requireParamSize(params, 1, 1);
    requireString(params[0U], "payloadId");
}

void parseExecutionPayloadRequest(Json::Value const& request)
{
    requireArray(request);
    auto const& params = request;
    requireParamSize(params, 1, 1);

    requireObject(params[0U], "executionPayload");
    auto const& payload = params[0U];
    requireStringField(payload, "parentHash");
    requireStringField(payload, "blockHash");
}

Json::Value mockArrayResult()
{
    return Json::Value(Json::arrayValue);
}

Json::Value mockObjectResult()
{
    return Json::Value(Json::objectValue);
}

task::Task<void> handleForkchoiceRequest(NodeService::Ptr nodeService, std::uint32_t version,
    Json::Value const& request, Json::Value& response)
{
    boost::ignore_unused(nodeService, version);
    requireArray(request);
    auto const& params = request;
    requireParamSize(params, 1, 2);

    parseForkchoiceState(params);
    parsePayloadAttributes(params);
    auto result = mockObjectResult();
    buildJsonContent(result, response);
    co_return;
}

task::Task<void> handleGetPayloadRequest(NodeService::Ptr nodeService, std::uint32_t version,
    Json::Value const& request, Json::Value& response)
{
    boost::ignore_unused(nodeService, version);
    parseGetPayloadRequest(request);
    auto result = mockObjectResult();
    buildJsonContent(result, response);
    co_return;
}

task::Task<void> handleNewPayloadRequest(NodeService::Ptr nodeService, std::uint32_t version,
    Json::Value const& request, Json::Value& response)
{
    boost::ignore_unused(nodeService, version);
    parseExecutionPayloadRequest(request);
    auto result = mockObjectResult();
    buildJsonContent(result, response);
    co_return;
}
}  // namespace

task::Task<void> EngineEndpoint::exchangeCapabilities(
    const Json::Value& request, Json::Value& response)
{
    auto capabilities = parseCapabilities(request);
    boost::ignore_unused(capabilities);
    auto result = mockArrayResult();
    buildJsonContent(result, response);
    co_return;
}

task::Task<void> EngineEndpoint::forkchoiceUpdatedV1(
    const Json::Value& request, Json::Value& response)
{
    co_await handleForkchoiceRequest(m_nodeService, 1, request, response);
}

task::Task<void> EngineEndpoint::forkchoiceUpdatedV2(
    const Json::Value& request, Json::Value& response)
{
    co_await handleForkchoiceRequest(m_nodeService, 2, request, response);
}

task::Task<void> EngineEndpoint::forkchoiceUpdatedV3(
    const Json::Value& request, Json::Value& response)
{
    co_await handleForkchoiceRequest(m_nodeService, 3, request, response);
}

task::Task<void> EngineEndpoint::getPayloadV2(const Json::Value& request, Json::Value& response)
{
    co_await handleGetPayloadRequest(m_nodeService, 2, request, response);
}

task::Task<void> EngineEndpoint::getPayloadV3(const Json::Value& request, Json::Value& response)
{
    co_await handleGetPayloadRequest(m_nodeService, 3, request, response);
}

task::Task<void> EngineEndpoint::newPayloadV2(const Json::Value& request, Json::Value& response)
{
    co_await handleNewPayloadRequest(m_nodeService, 2, request, response);
}

task::Task<void> EngineEndpoint::newPayloadV3(const Json::Value& request, Json::Value& response)
{
    co_await handleNewPayloadRequest(m_nodeService, 3, request, response);
}