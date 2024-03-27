/**
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief interface for RPC
 * @file Rpc.h
 * @author: octopus
 * @date 2021-07-15
 */

#pragma once
#include "bcos-rpc/amop/AMOPClient.h"
#include "bcos-rpc/amop/AirAMOPClient.h"
#include "bcos-rpc/groupmgr/AirGroupManager.h"
#include "bcos-rpc/groupmgr/GroupManager.h"
#include "web3jsonrpc/Web3JsonRpcImpl.h"

#include <bcos-boostssl/websocket/WsConfig.h>
#include <bcos-crypto/interfaces/crypto/KeyFactory.h>
#include <bcos-framework/consensus/ConsensusInterface.h>
#include <bcos-framework/election/LeaderEntryPointInterface.h>
#include <bcos-framework/gateway/GatewayInterface.h>
#include <bcos-framework/security/DataEncryptInterface.h>
#include <bcos-rpc/Common.h>
#include <bcos-rpc/Rpc.h>
#include <bcos-rpc/event/EventSub.h>
#include <bcos-rpc/jsonrpc/JsonRpcImpl_2_0.h>
#include <bcos-tool/NodeConfig.h>

namespace bcos
{
namespace boostssl::ws
{
class WsService;
class WsSession;
}  // namespace boostssl::ws

namespace rpc
{
class RpcFactory : public std::enable_shared_from_this<RpcFactory>
{
public:
    using Ptr = std::shared_ptr<RpcFactory>;
    RpcFactory(std::string _chainID, bcos::gateway::GatewayInterface::Ptr _gatewayInterface,
        bcos::crypto::KeyFactory::Ptr _keyFactory,
        bcos::security::DataEncryptInterface::Ptr _dataEncrypt = nullptr);
    virtual ~RpcFactory() = default;

    std::shared_ptr<boostssl::ws::WsConfig> initConfig(
        const bcos::tool::NodeConfig::Ptr& _nodeConfig);
    std::shared_ptr<boostssl::ws::WsConfig> initWeb3RpcServiceConfig(
        const bcos::tool::NodeConfig::Ptr& _nodeConfig);
    std::shared_ptr<boostssl::ws::WsService> buildWsService(
        bcos::boostssl::ws::WsConfig::Ptr _config);

    Rpc::Ptr buildRpc(std::string const& _gatewayServiceName, std::string const& _rpcServiceName,
        bcos::election::LeaderEntryPointInterface::Ptr _entryPoint);
    Rpc::Ptr buildLocalRpc(bcos::group::GroupInfo::Ptr _groupInfo, NodeService::Ptr _nodeService);

    Rpc::Ptr buildRpc(int sendTxTimeout, std::shared_ptr<boostssl::ws::WsService> _wsService,
        GroupManager::Ptr _groupManager, AMOPClient::Ptr _amopClient);

    bcos::tool::NodeConfig::Ptr nodeConfig() const { return m_nodeConfig; }
    void setNodeConfig(bcos::tool::NodeConfig::Ptr _nodeConfig) { m_nodeConfig = _nodeConfig; }

protected:
    // for groupManager builder
    GroupManager::Ptr buildGroupManager(std::string const& _rpcServiceName,
        bcos::election::LeaderEntryPointInterface::Ptr _entryPoint);
    AirGroupManager::Ptr buildAirGroupManager(
        bcos::group::GroupInfo::Ptr _groupInfo, NodeService::Ptr _nodeService);

    // for AMOP builder
    AMOPClient::Ptr buildAMOPClient(std::shared_ptr<boostssl::ws::WsService> _wsService,
        std::string const& _gatewayServiceName);
    AMOPClient::Ptr buildAirAMOPClient(std::shared_ptr<boostssl::ws::WsService> _wsService);


    bcos::rpc::JsonRpcImpl_2_0::Ptr buildJsonRpc(int sendTxTimeout,
        const std::shared_ptr<boostssl::ws::WsService>& _wsService,
        GroupManager::Ptr _groupManager);

    bcos::rpc::Web3JsonRpcImpl::Ptr buildWeb3JsonRpc(int sendTxTimeout,
        boostssl::ws::WsService::Ptr _wsService, GroupManager::Ptr _groupManager);
    bcos::event::EventSub::Ptr buildEventSub(
        const std::shared_ptr<boostssl::ws::WsService>& _wsService,
        GroupManager::Ptr _groupManager);

private:
    std::string m_chainID;
    bcos::gateway::GatewayInterface::Ptr m_gateway;
    std::shared_ptr<bcos::crypto::KeyFactory> m_keyFactory;
    bcos::tool::NodeConfig::Ptr m_nodeConfig;
    bcos::security::DataEncryptInterface::Ptr m_dataEncrypt;
};
}  // namespace rpc
}  // namespace bcos