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
 * @brief initializer for the RpcService
 * @file RpcInitializer.cpp
 * @author: yujiechen
 * @date 2021-10-15
 */
#include "RpcInitializer.h"
#include "Common/TarsUtils.h"
#include "libinitializer/ProtocolInitializer.h"
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-framework//election/FailOverTypeDef.h>
#ifdef ETCD
#include <bcos-leader-election/src/LeaderEntryPoint.h>
#endif
#include <bcos-rpc/RpcFactory.h>
#include <bcos-tars-protocol/client/GatewayServiceClient.h>
#include <bcos-tars-protocol/protocol/MemberImpl.h>

using namespace bcos::group;
using namespace bcostars;

void RpcInitializer::init(std::string const& _configDir)
{
    // init node config
    RPCSERVICE_LOG(INFO) << LOG_DESC("init node config") << LOG_KV("configDir", _configDir);

    if (!m_nodeConfig->rpcSmSsl())
    {
        m_nodeConfig->setCaCert(_configDir + "/" + "ca.crt");
        m_nodeConfig->setNodeCert(_configDir + "/" + "ssl.crt");
        m_nodeConfig->setNodeKey(_configDir + "/" + "ssl.key");
    }
    else
    {
        m_nodeConfig->setSmCaCert(_configDir + "/" + "sm_ca.crt");
        m_nodeConfig->setSmNodeCert(_configDir + "/" + "sm_ssl.crt");
        m_nodeConfig->setSmNodeKey(_configDir + "/" + "sm_ssl.key");
        m_nodeConfig->setEnSmNodeCert(_configDir + "/" + "sm_enssl.crt");
        m_nodeConfig->setEnSmNodeKey(_configDir + "/" + "sm_enssl.key");
    }
#ifdef ETCD
    if (m_nodeConfig->enableFailOver())
    {
        RPCSERVICE_LOG(INFO) << LOG_DESC("enable failover");
        auto memberFactory = std::make_shared<bcostars::protocol::MemberFactoryImpl>();
        auto leaderEntryPointFactory =
            std::make_shared<bcos::election::LeaderEntryPointFactoryImpl>(memberFactory);
        auto watchDir = "/" + m_nodeConfig->chainId() + bcos::election::CONSENSUS_LEADER_DIR;
        m_leaderEntryPoint = leaderEntryPointFactory->createLeaderEntryPoint(
            m_nodeConfig->failOverClusterUrl(), watchDir, "watchLeaderChange");
    }
#endif
    // init rpc config
    RPCSERVICE_LOG(INFO) << LOG_DESC("init rpc factory");
    auto factory = initRpcFactory(m_nodeConfig);

    auto rpcServiceName = bcostars::getProxyDesc(bcos::protocol::RPC_SERVANT_NAME);
    RPCSERVICE_LOG(INFO) << LOG_DESC("init rpc factory success")
                         << LOG_KV("rpcServiceName", rpcServiceName);
    auto rpc =
        factory->buildRpc(m_nodeConfig->gatewayServiceName(), rpcServiceName, m_leaderEntryPoint);
    m_rpc = rpc;
}

void RpcInitializer::setClientID(std::string const& _clientID)
{
    m_rpc->setClientID(_clientID);
}

bcos::rpc::RPCInterface::Ptr RpcInitializer::rpc()
{
    return m_rpc;
}

bcos::rpc::RpcFactory::Ptr RpcInitializer::initRpcFactory(bcos::tool::NodeConfig::Ptr _nodeConfig)
{
    // init the protocol
    auto protocolInitializer = std::make_shared<bcos::initializer::ProtocolInitializer>();
    protocolInitializer->init(_nodeConfig);
    m_keyFactory = protocolInitializer->keyFactory();

    // get the gateway client
    auto gatewayPrx = Application::getCommunicator()->stringToProxy<GatewayServicePrx>(
        _nodeConfig->gatewayServiceName());
    auto gateway = std::make_shared<GatewayServiceClient>(
        gatewayPrx, _nodeConfig->gatewayServiceName(), protocolInitializer->keyFactory());

    auto factory = std::make_shared<bcos::rpc::RpcFactory>(
        _nodeConfig->chainId(), gateway, protocolInitializer->keyFactory());
    factory->setNodeConfig(_nodeConfig);
    RPCSERVICE_LOG(INFO) << LOG_DESC("create rpc factory success");
    return factory;
}

void RpcInitializer::start()
{
    if (m_running)
    {
        RPCSERVICE_LOG(INFO) << LOG_DESC("The RpcService has already been started");
        return;
    }
    m_running = true;

#ifdef ETCD
    if (m_leaderEntryPoint)
    {
        RPCSERVICE_LOG(INFO) << LOG_DESC("start leader-entry-point");
        m_leaderEntryPoint->start();
    }
#endif
    RPCSERVICE_LOG(INFO) << LOG_DESC("start rpc");
    m_rpc->start();
    RPCSERVICE_LOG(INFO) << LOG_DESC("start rpc success");
}

void RpcInitializer::stop()
{
    if (!m_running)
    {
        RPCSERVICE_LOG(WARNING) << LOG_DESC("The RpcService has already stopped!");
        return;
    }
    m_running = false;
    RPCSERVICE_LOG(INFO) << LOG_DESC("Stop the RpcService");

#ifdef ETCD
    if (m_leaderEntryPoint)
    {
        m_leaderEntryPoint->stop();
    }
#endif

    if (m_rpc)
    {
        m_rpc->stop();
    }
    RPCSERVICE_LOG(INFO) << LOG_DESC("Stop the RpcService success");
}
