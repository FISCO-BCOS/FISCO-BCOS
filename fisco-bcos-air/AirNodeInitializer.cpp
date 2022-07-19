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
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-framework/protocol/GlobalConfig.h>
#include <bcos-gateway/GatewayFactory.h>
#include <bcos-gateway/libamop/AirTopicManager.h>
#include <bcos-rpc/RpcFactory.h>
#include <bcos-rpc/groupmgr/NodeService.h>
#include <bcos-scheduler/src/SchedulerImpl.h>
#include <bcos-tars-protocol/protocol/ProtocolInfoCodecImpl.h>
#include <bcos-tool/NodeConfig.h>
using namespace bcos::node;
using namespace bcos::initializer;
using namespace bcos::gateway;
using namespace bcos::rpc;
using namespace bcos::tool;

void AirNodeInitializer::init(std::string const& _configFilePath, std::string const& _genesisFile)
{
    INITIALIZER_LOG(INFO) << LOG_DESC("initGlobalConfig");
    g_BCOSConfig.setCodec(std::make_shared<bcostars::protocol::ProtocolInfoCodecImpl>());

    boost::property_tree::ptree pt;
    boost::property_tree::read_ini(_configFilePath, pt);

    m_logInitializer = std::make_shared<BoostLogInitializer>();
    m_logInitializer->initLog(pt);

    // load nodeConfig
    // Note: this NodeConfig is used to create Gateway which not init the nodeName
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    auto nodeConfig = std::make_shared<NodeConfig>(keyFactory);
    nodeConfig->loadConfig(_configFilePath);
    nodeConfig->loadGenesisConfig(_genesisFile);

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
    auto topicManager =
        std::dynamic_pointer_cast<bcos::amop::LocalTopicManager>(gateway->amop()->topicManager());
    topicManager->setLocalClient(m_rpc);
    m_nodeInitializer->initNotificationHandlers(m_rpc);

    // NOTE: this should be last called
    m_nodeInitializer->initSysContract();
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
    }
    catch (std::exception const& e)
    {
        std::cout << "stop bcos-node failed for " << boost::diagnostic_information(e);
        exit(-1);
    }
}