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
#include "bcos-rpc/web3jsonrpc/endpoints/EndpointInterface.h"
#include "bcos-rpc/web3jsonrpc/endpoints/EthEndpoint.h"
#include "bcos-rpc/web3jsonrpc/endpoints/NetEndpoint.h"
#include "bcos-rpc/web3jsonrpc/endpoints/Web3Endpoint.h"
#include <bcos-boostssl/websocket/WsService.h>
#include <bcos-framework/gateway/GatewayInterface.h>
#include <bcos-rpc/jsonrpc/JsonRpcInterface.h>
#include <json/json.h>
#include <tbb/concurrent_hash_map.h>
#include <boost/core/ignore_unused.hpp>
#include <unordered_map>

namespace bcos::rpc
{
class EthEndpoint;
class NetEndpoint;
class Web3Endpoint;
class Web3JsonRpcImpl : public JsonRpcImpl_2_0, public std::enable_shared_from_this<Web3JsonRpcImpl>
{
public:
    Web3JsonRpcImpl(bcos::rpc::GroupManager::Ptr _groupManager,
        bcos::gateway::GatewayInterface::Ptr _gatewayInterface,
        std::shared_ptr<boostssl::ws::WsService> _wsService)
      : JsonRpcImpl_2_0(
            std::move(_groupManager), std::move(_gatewayInterface), std::move(_wsService)),
        m_ethEndpoint(std::make_unique<EthEndpoint>(m_groupManager)),
        m_netEndpoint(std::make_unique<NetEndpoint>(m_groupManager)),
        m_web3Endpoint(std::make_unique<Web3Endpoint>(m_groupManager))
    {
        auto&& ethMap = m_ethEndpoint->exportMethods();
        auto&& netMap = m_netEndpoint->exportMethods();
        auto&& web3Map = m_web3Endpoint->exportMethods();
        m_methodToFunc.insert(ethMap.begin(), ethMap.end());
        m_methodToFunc.insert(netMap.begin(), netMap.end());
        m_methodToFunc.insert(web3Map.begin(), web3Map.end());
    }
    ~Web3JsonRpcImpl() override = default;

private:
    EthEndpoint::UniquePtr m_ethEndpoint;
    NetEndpoint::UniquePtr m_netEndpoint;
    Web3Endpoint::UniquePtr m_web3Endpoint;
};
}  // namespace bcos::rpc
