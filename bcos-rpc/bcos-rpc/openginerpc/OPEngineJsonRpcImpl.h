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
 * @file OPEngineJsonRpcImpl.h
 * @date 2026/5/20
 */

#pragma once

#include "endpoints/OPEngineEndpoints.h"
#include "endpoints/OPEngineEndpointsMapping.h"
#include <bcos-framework/engine/AnyEngineService.h>
#include "bcos-framework/gateway/GatewayInterface.h"
#include <bcos-boostssl/httpserver/Common.h>
#include <bcos-rpc/jwtAuth/JwtVerifier.h>
#include <bcos-rpc/filter/FilterSystem.h>
#include <bcos-rpc/groupmgr/GroupManager.h>
#include <bcos-rpc/web3jsonrpc/endpoints/Endpoints.h>
#include <bcos-rpc/web3jsonrpc/endpoints/EndpointsMapping.h>
#include <bcos-boostssl/websocket/WsService.h>
#include <json/json.h>

namespace bcos::rpc
{
class OPEngineJsonRpcImpl : public std::enable_shared_from_this<OPEngineJsonRpcImpl>
{
public:
    using Ptr = std::shared_ptr<OPEngineJsonRpcImpl>;
    using Sender = std::function<void(bcos::bytes)>;

    OPEngineJsonRpcImpl(std::string const& _groupId, uint32_t _batchRequestSizeLimit,
        GroupManager::Ptr const& _groupManager, FilterSystem::Ptr _filterSystem,
        bool _syncTransaction);
    ~OPEngineJsonRpcImpl() = default;

    void setEngineService(std::shared_ptr<bcos::engine::AnyEngineService> _engineService);
    void setJwtVerifier(bcos::rpc::JwtVerifier::Ptr _jwtVerifier);

    void onRPCRequest(std::string_view _requestBody, const bcos::boostssl::http::HttpRequestMeta& _meta, Sender _sender);

    OPEngineEndpoints& endpoints() { return m_endpoints; }
    Endpoints& web3Endpoints() { return m_web3Endpoints; }

private:
    task::Task<Json::Value> handleRequest(Json::Value _request);
    task::Task<Json::Value> handleBatchRequest(Json::Value _request);

    uint32_t m_batchRequestSizeLimit;
    bcos::rpc::JwtVerifier::Ptr m_jwtVerifier;
    OPEngineEndpoints m_endpoints;
    OPEngineEndpointsMapping m_endpointsMapping;
    Endpoints m_web3Endpoints;
    EndpointsMapping m_web3EndpointsMapping;
};
}  // namespace bcos::rpc
