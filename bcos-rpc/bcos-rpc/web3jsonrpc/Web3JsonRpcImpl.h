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
 * @file Web3JsonRpcImpl.h
 * @author: kyonGuo
 * @date 2024/3/21
 */

#pragma once
#include <bcos-boostssl/websocket/WsService.h>
#include <bcos-framework/gateway/GatewayInterface.h>
#include <bcos-rpc/groupmgr/GroupManager.h>
#include <bcos-rpc/validator/JsonValidator.h>
#include <bcos-rpc/web3jsonrpc/endpoints/Endpoints.h>
#include <bcos-rpc/web3jsonrpc/endpoints/EndpointsMapping.h>
#include <bcos-rpc/web3jsonrpc/utils/util.h>
#include <json/json.h>
namespace bcos::rpc
{
class Web3JsonRpcImpl : public std::enable_shared_from_this<Web3JsonRpcImpl>
{
public:
    using Ptr = std::shared_ptr<Web3JsonRpcImpl>;
    using Sender = std::function<void(bcos::bytes)>;
    Web3JsonRpcImpl(std::string _groupId, uint32_t _batchRequestSizeLimit,
        bcos::rpc::GroupManager::Ptr _groupManager,
        bcos::gateway::GatewayInterface::Ptr _gatewayInterface,
        std::shared_ptr<boostssl::ws::WsService> _wsService, FilterSystem::Ptr filterSystem);
    ~Web3JsonRpcImpl() { RPC_LOG(INFO) << LOG_KV("[DELOBJ][Web3JsonRpcImpl]", this); }

    void onRPCRequest(std::string_view _requestBody, const Sender& _sender);

    void handleRequest(Json::Value _request, const std::function<void(Json::Value)>& _callback);
    void handleRequest(Json::Value _request, const Sender& _sender);
    void handleBatchRequest(Json::Value _request, const Sender& _sender);

private:
    static bcos::bytes toBytesResponse(Json::Value const& jResp);
    // Note: only use in one group
    GroupManager::Ptr m_groupManager;
    bcos::gateway::GatewayInterface::Ptr m_gatewayInterface;
    std::shared_ptr<boostssl::ws::WsService> m_wsService;
    std::string m_groupId;
    uint32_t m_batchRequestSizeLimit;
    Endpoints m_endpoints;
    EndpointsMapping m_endpointsMapping;
};
}  // namespace bcos::rpc
