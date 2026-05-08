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

#include <bcos-rpc/web3jsonrpc/utils/util.h>

using namespace bcos;
using namespace bcos::rpc;

namespace
{
Json::Value mockArrayResult()
{
    return {Json::arrayValue};
}

Json::Value mockObjectResult()
{
    return {Json::objectValue};
}

task::Task<void> handleForkchoiceRequest(NodeService::Ptr nodeService, std::uint32_t version,
    Json::Value const& request, Json::Value& response)
{
    boost::ignore_unused(nodeService, version, request);
    auto result = mockObjectResult();
    buildJsonContent(result, response);
    co_return;
}

task::Task<void> handleGetPayloadRequest(NodeService::Ptr nodeService, std::uint32_t version,
    Json::Value const& request, Json::Value& response)
{
    boost::ignore_unused(nodeService, version, request);
    auto result = mockObjectResult();
    buildJsonContent(result, response);
    co_return;
}

task::Task<void> handleNewPayloadRequest(NodeService::Ptr nodeService, std::uint32_t version,
    Json::Value const& request, Json::Value& response)
{
    boost::ignore_unused(nodeService, version, request);
    auto result = mockObjectResult();
    buildJsonContent(result, response);
    co_return;
}
}  // namespace

EngineEndpoint::EngineEndpoint(NodeService::Ptr nodeService) : m_nodeService(std::move(nodeService))
{}

task::Task<void> EngineEndpoint::exchangeCapabilities(
    const Json::Value& request, Json::Value& response)
{
    boost::ignore_unused(request);
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
