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
 * @brief initializer for the GatewayService
 * @file GatewayInitializer.cpp
 * @author: yujiechen
 * @date 2021-10-15
 */
#include "GatewayInitializer.h"
#include "../Common/TarsUtils.h"
#include "libinitializer/ProtocolInitializer.h"
#include <bcos-framework/election/FailOverTypeDef.h>
#include <bcos-framework/protocol/GlobalConfig.h>
#include <bcos-gateway/Gateway.h>
#include <bcos-gateway/GatewayConfig.h>
#include <bcos-gateway/GatewayFactory.h>
#ifdef WITH_LEDGER_ELECTION
#include <bcos-leader-election/src/LeaderEntryPoint.h>
#endif
#include <bcos-tars-protocol/protocol/MemberImpl.h>
#include <bcos-tars-protocol/protocol/ProtocolInfoCodecImpl.h>
#include <bcos-tool/NodeConfig.h>
// #include "bcos-framework/security/KeyEncryptionType.h"

using namespace tars;
using namespace bcostars;

void GatewayInitializer::init(std::string const& _configPath)
{
    GATEWAYSERVICE_LOG(INFO) << LOG_DESC("initGlobalConfig");
    bcos::protocol::g_BCOSConfig.setCodec(
        std::make_shared<bcostars::protocol::ProtocolInfoCodecImpl>());

    GATEWAYSERVICE_LOG(INFO) << LOG_DESC("initGateWayConfig") << LOG_KV("configPath", _configPath);

    GATEWAYSERVICE_LOG(INFO) << LOG_DESC("load nodeConfig");
    auto nodeConfig = std::make_shared<bcos::tool::NodeConfig>();
    nodeConfig->loadConfig(_configPath, false, true, false);

    boost::property_tree::ptree pt;
    boost::property_tree::read_ini(_configPath, pt);
    nodeConfig->loadServiceConfig(pt);
    GATEWAYSERVICE_LOG(INFO) << LOG_DESC("load nodeConfig success");
#ifdef WITH_LEDGER_ELECTION
    if (nodeConfig->enableFailOver())
    {
        GATEWAYSERVICE_LOG(INFO) << LOG_DESC("enable failover");
        auto memberFactory = std::make_shared<bcostars::protocol::MemberFactoryImpl>();
        auto leaderEntryPointFactory =
            std::make_shared<bcos::election::LeaderEntryPointFactoryImpl>(memberFactory);
        auto watchDir = "/" + nodeConfig->chainId() + bcos::election::CONSENSUS_LEADER_DIR;
        m_leaderEntryPoint = leaderEntryPointFactory->createLeaderEntryPoint(
            nodeConfig->failOverClusterUrl(), watchDir, "watchLeaderChange", nodeConfig->pdCaPath(),
            nodeConfig->pdCertPath(), nodeConfig->pdKeyPath());
    }
#endif

    auto protocolInitializer = std::make_shared<bcos::initializer::ProtocolInitializer>();
    protocolInitializer->init(nodeConfig);

    bcos::gateway::GatewayFactory factory(nodeConfig->chainId(), nodeConfig->rpcServiceName(),
        protocolInitializer->getKeyEncryptionByType(nodeConfig->keyEncryptionType()));
    auto gatewayServiceName = bcostars::getProxyDesc(bcos::protocol::GATEWAY_SERVANT_NAME);
    GATEWAYSERVICE_LOG(INFO) << LOG_DESC("buildGateWay")
                             << LOG_KV("certPath", m_gatewayConfig->certPath())
                             << LOG_KV("nodePath", m_gatewayConfig->nodePath())
                             << LOG_KV("gatewayServiceName", gatewayServiceName);
    auto gateway =
        factory.buildGateway(m_gatewayConfig, false, m_leaderEntryPoint, gatewayServiceName);

    m_gateway = gateway;
    GATEWAYSERVICE_LOG(INFO) << LOG_DESC("buildGateway success");
}

void GatewayInitializer::start()
{
    if (m_running)
    {
        GATEWAYSERVICE_LOG(INFO) << LOG_DESC("the gateway has already been started");
        return;
    }
    m_running = true;
    if (m_leaderEntryPoint)
    {
        GATEWAYSERVICE_LOG(INFO) << LOG_DESC("start leader-entry-point");
        m_leaderEntryPoint->start();
    }
    // start the gateway
    GATEWAYSERVICE_LOG(INFO) << LOG_DESC("start the gateway");
    m_gateway->start();
    GATEWAYSERVICE_LOG(INFO) << LOG_DESC("start the gateway success");
}

void GatewayInitializer::stop()
{
    if (!m_running)
    {
        GATEWAYSERVICE_LOG(INFO) << LOG_DESC("The GatewayService has already been stopped");
        return;
    }
    m_running = false;
    GATEWAYSERVICE_LOG(INFO) << LOG_DESC("Stop the GatewayService");
    if (m_leaderEntryPoint)
    {
        m_leaderEntryPoint->stop();
    }
    if (m_gateway)
    {
        m_gateway->stop();
    }
    TLOGINFO(LOG_DESC("[GATEWAYSERVICE] Stop the GatewayService success") << std::endl);
}
bcostars::GatewayInitializer::GatewayInitializer(
    std::string const& _configPath, bcos::gateway::GatewayConfig::Ptr _gatewayConfig)
  : m_gatewayConfig(_gatewayConfig),
    m_keyFactory(std::make_shared<bcos::crypto::KeyFactoryImpl>()),
    m_groupInfoFactory(std::make_shared<bcos::group::GroupInfoFactory>()),
    m_chainNodeInfoFactory(std::make_shared<bcos::group::ChainNodeInfoFactory>())
{
    init(_configPath);
}
bcostars::GatewayInitializer::~GatewayInitializer()
{
    stop();
}
bcos::gateway::GatewayInterface::Ptr bcostars::GatewayInitializer::gateway()
{
    return m_gateway;
}
bcos::group::ChainNodeInfoFactory::Ptr bcostars::GatewayInitializer::chainNodeInfoFactory()
{
    return m_chainNodeInfoFactory;
}
bcos::group::GroupInfoFactory::Ptr bcostars::GatewayInitializer::groupInfoFactory()
{
    return m_groupInfoFactory;
}
bcos::crypto::KeyFactory::Ptr bcostars::GatewayInitializer::keyFactory()
{
    return m_keyFactory;
}
