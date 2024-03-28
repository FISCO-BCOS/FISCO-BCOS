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
#include "bcos-rpc/groupmgr/GroupManager.h"
#include "bcos-rpc/jsonrpc/JsonRpcImpl_2_0.h"
#include "bcos-rpc/validator/CallValidator.h"
#include <bcos-boostssl/websocket/WsService.h>
#include <bcos-framework/gateway/GatewayInterface.h>
#include <bcos-rpc/jsonrpc/JsonRpcInterface.h>
#include <bcos-rpc/web3jsonrpc/endpoints/Endpoints.h>
#include <bcos-rpc/web3jsonrpc/endpoints/EndpointsMapping.h>
#include <json/json.h>
#include <boost/core/ignore_unused.hpp>
#include <unordered_map>

namespace bcos::rpc
{
class Web3JsonRpcImpl : public JsonRpcImpl_2_0, public std::enable_shared_from_this<Web3JsonRpcImpl>
{
public:
    Web3JsonRpcImpl(std::string _groupId, bcos::rpc::GroupManager::Ptr _groupManager,
        bcos::gateway::GatewayInterface::Ptr _gatewayInterface,
        std::shared_ptr<boostssl::ws::WsService> _wsService)
      : JsonRpcImpl_2_0(
            std::move(_groupManager), std::move(_gatewayInterface), std::move(_wsService)),
        m_groupId(std::move(_groupId)),
        m_endpoints(_groupManager->getNodeService(m_groupId, ""))
    {}
    ~Web3JsonRpcImpl() override = default;

private:
    // Note: only use in one group
    std::string m_groupId;
    Endpoints m_endpoints;
    EndpointsMapping m_endpointsMapping;
};
}  // namespace bcos::rpc
