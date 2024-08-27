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
 * @brief Initializer for all the modules
 * @file LocalInitializer.cpp
 * @author: yujiechen
 * @date 2021-10-28
 */
#include "AirNodeInitializer.h"
#include "bcos-gateway/libnetwork/Session.h"
#include "bcos-gateway/libnetwork/Socket.h"
#include "bcos-gateway/libp2p/P2PMessageV2.h"
#include "bcos-utilities/ratelimiter/DistributedRateLimiter.h"
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-framework/protocol/GlobalConfig.h>
#include <bcos-gateway/GatewayFactory.h>
#include <bcos-gateway/libamop/AirTopicManager.h>
#include <bcos-rpc/RpcFactory.h>
#include <bcos-rpc/groupmgr/NodeService.h>
#include <bcos-rpc/tarsRPC/RPCServer.h>
#include <bcos-tars-protocol/protocol/ProtocolInfoCodecImpl.h>
#include <bcos-tool/NodeConfig.h>
#include <rocksdb/env.h>

using namespace bcos::node;
using namespace bcos::initializer;
using namespace bcos::gateway;
using namespace bcos::rpc;
using namespace bcos::tool;

void AirNodeInitializer::init(std::string const& _configFilePath, std::string const& _genesisFile)
{
    g_BCOSConfig.setCodec(std::make_shared<bcostars::protocol::ProtocolInfoCodecImpl>());

    boost::property_tree::ptree ptree;
    boost::property_tree::read_ini(_configFilePath, ptree);

    m_logInitializer = std::make_shared<BoostLogInitializer>();
    m_logInitializer->initLog(_configFilePath);
    INITIALIZER_LOG(INFO) << LOG_DESC("initGlobalConfig");

    // load nodeConfig
    // Note: this NodeConfig is used to create Gateway which not init the nodeName
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    auto nodeConfig = std::make_shared<NodeConfig>(keyFactory);
    nodeConfig->loadGenesisConfig(_genesisFile);
    nodeConfig->loadConfig(_configFilePath);

    m_nodeInitializer = std::make_shared<bcos::initializer::Initializer>();
    m_nodeInitializer->initConfig(_configFilePath, _genesisFile, "", true);

    // create gateway
    // DataEncryption will be inited in ProtocolInitializer when storage_security.enable = true,
    // otherwise dataEncryption() will return nullptr
    GatewayFactory gatewayFactory(nodeConfig->chainId(), "localRpc",
        m_nodeInitializer->protocolInitializer()->dataEncryption());
    auto gateway = gatewayFactory.buildGateway(_configFilePath, true, nullptr, "localGateway");
    m_gateway = gateway;

    // create the node
    m_nodeInitializer->init(bcos::protocol::NodeArchitectureType::AIR, _configFilePath,
        _genesisFile, m_gateway, true, m_logInitializer->logPath());

    auto pbftInitializer = m_nodeInitializer->pbftInitializer();
    auto groupInfo = m_nodeInitializer->pbftInitializer()->groupInfo();
    auto nodeService =
        std::make_shared<NodeService>(m_nodeInitializer->ledger(), m_nodeInitializer->scheduler(),
            m_nodeInitializer->txPoolInitializer()->txpool(), pbftInitializer->pbft(),
            pbftInitializer->blockSync(), m_nodeInitializer->protocolInitializer()->blockFactory());

    // create rpc
    RpcFactory rpcFactory(nodeConfig->chainId(), m_gateway, keyFactory,
        m_nodeInitializer->protocolInitializer()->dataEncryption());
    rpcFactory.setNodeConfig(nodeConfig);
    m_rpc = rpcFactory.buildLocalRpc(groupInfo, nodeService);
    if (gateway->amop())
    {
        auto topicManager = std::dynamic_pointer_cast<bcos::amop::LocalTopicManager>(
            gateway->amop()->topicManager());
        topicManager->setLocalClient(m_rpc);
    }
    m_nodeInitializer->initNotificationHandlers(m_rpc);

    m_objMonitor = std::make_shared<bcos::ObjectAllocatorMonitor>();

    // NOTE: this should be last called
    m_nodeInitializer->initSysContract();

    // tars rpc
    if (!nodeConfig->tarsRPCConfig().host.empty() && nodeConfig->tarsRPCConfig().port > 0 &&
        nodeConfig->tarsRPCConfig().threadCount > 0)
    {
        m_tarsApplication.emplace(nodeService);
        m_tarsConfig.emplace(RPCApplication::generateTarsConfig(nodeConfig->tarsRPCConfig().host,
            nodeConfig->tarsRPCConfig().port, nodeConfig->tarsRPCConfig().threadCount));
    }
}

void AirNodeInitializer::start()
{
    if (m_nodeInitializer)
    {
        m_nodeInitializer->start();
    }

    if (m_gateway)
    {
        m_gateway->start();
    }

    if (m_rpc)
    {
        m_rpc->start();
    }

    if (m_objMonitor)
    {
        // start monitor object alloc
        m_objMonitor->startMonitor</*boostssl start*/ bcos::boostssl::ws::WsMessage,
            bcos::boostssl::ws::WsSession, bcos::boostssl::ws::RawWsStream,
            bcos::boostssl::ws::SslWsStream, bcos::boostssl::ws::WsSession::CallBack,
            bcos::boostssl::ws::WsSession::Message,
            bcos::boostssl::ws::WsStreamDelegate /*boostssl end*/,
            /*gateway start*/ bcos::gateway::Session, bcos::gateway::Socket,
            bcos::gateway::EncodedMessage, bcos::gateway::SessionRecvBuffer,
            bcos::gateway::P2PMessage, bcos::gateway::P2PSession, bcos::gateway::P2PMessageV2,
            bcos::gateway::FrontServiceInfo, bcos::gateway::GatewayNodeStatus,
            bcos::gateway::GatewayStatus, bcos::gateway::ResponseCallback, bcos::gateway::Retry,
            bcos::ratelimiter::TimeWindowRateLimiter,
            bcos::ratelimiter::DistributedRateLimiter /*gateway end*/>(4);
    }

    if (m_tarsApplication && m_tarsConfig)
    {
        boost::atomic_bool started = false;
        m_tarsThread.emplace([&, this]() {
            m_tarsApplication->main(*m_tarsConfig);
            started = true;
            started.notify_all();
            m_tarsApplication->waitForShutdown();
        });

        started.wait(false);
    }
}

void AirNodeInitializer::stop()
{
    try
    {
        if (m_rpc)
        {
            m_rpc->stop();
        }
        if (m_gateway)
        {
            m_gateway->stop();
        }
        if (m_nodeInitializer)
        {
            m_nodeInitializer->stop();
        }
        if (m_tarsApplication && m_tarsThread)
        {
            m_tarsApplication->terminate();
            m_tarsThread->join();
        }

        if (m_objMonitor)
        {
            m_objMonitor->stopMonitor();
        }
    }
    catch (std::exception const& e)
    {
        std::cout << "stop bcos-node failed for " << boost::diagnostic_information(e);
        exit(-1);
    }
}
