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
#include "LocalNodeInitializer.h"
#include <bcos-framework/libtool/NodeConfig.h>
#include <bcos-gateway/GatewayFactory.h>
#include <bcos-gateway/libamop/LocalTopicManager.h>
#include <bcos-rpc/RpcFactory.h>
using namespace bcos::node;
using namespace bcos::initializer;
using namespace bcos::gateway;
using namespace bcos::rpc;
using namespace bcos::tool;

void LocalNodeInitializer::init(std::string const& _configFilePath, std::string const& _genesisFile)
{
    boost::property_tree::ptree pt;
    boost::property_tree::read_ini(_configFilePath, pt);
    m_logInitializer = std::make_shared<BoostLogInitializer>();
    m_logInitializer->initLog(pt);

    // load nodeConfig
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    auto nodeConfig = std::make_shared<NodeConfig>(keyFactory);
    nodeConfig->loadConfig(_configFilePath);
    nodeConfig->loadGenesisConfig(_genesisFile);

    // create gateway
    GatewayFactory gatewayFactory(nodeConfig->chainId(), "localRpc");
    auto gateway = gatewayFactory.buildGateway(_configFilePath, true);
    m_gateway = gateway;

    // create the node
    initLocalNode(_configFilePath, _genesisFile, m_gateway);
    auto nodeID = m_nodeInitializer->protocolInitializer()->keyPair()->publicKey();
    gateway->registerFrontService(
        nodeConfig->groupId(), nodeID, m_nodeInitializer->frontService()->front());

    auto pbftInitializer = m_nodeInitializer->pbftInitializer();
    auto groupInfo = m_nodeInitializer->pbftInitializer()->groupInfo();
    auto nodeService =
        std::make_shared<NodeService>(m_nodeInitializer->ledger(), m_nodeInitializer->scheduler(),
            m_nodeInitializer->txPoolInitializer()->txpool(), pbftInitializer->pbft(),
            pbftInitializer->blockSync(), m_nodeInitializer->protocolInitializer()->blockFactory());
    // create rpc
    RpcFactory rpcFactory(nodeConfig->chainId(), m_gateway, keyFactory);
    rpcFactory.setNodeConfig(nodeConfig);
    m_rpc = rpcFactory.buildLocalRpc(groupInfo, nodeService);
    auto topicManager =
        std::dynamic_pointer_cast<bcos::amop::LocalTopicManager>(gateway->amop()->topicManager());
    topicManager->setLocalClient(m_rpc);

    // init handlers
    auto schedulerImpl =
        std::dynamic_pointer_cast<scheduler::SchedulerImpl>(m_nodeInitializer->scheduler());
    schedulerImpl->registerBlockNumberReceiver(
        [rpc = m_rpc, nodeConfig](bcos::protocol::BlockNumber number) {
            BCOS_LOG(INFO) << "Notify blocknumber: " << number;
            rpc->asyncNotifyBlockNumber(
                nodeConfig->groupId(), nodeConfig->nodeName(), number, [](bcos::Error::Ptr) {});
        });

    auto txpool = m_nodeInitializer->txPoolInitializer()->txpool();
    schedulerImpl->registerTransactionNotifier(
        [txpool](bcos::protocol::BlockNumber _blockNumber,
            bcos::protocol::TransactionSubmitResultsPtr _result,
            std::function<void(bcos::Error::Ptr)> _callback) {
            txpool->asyncNotifyBlockResult(_blockNumber, _result, _callback);
        });
}

void LocalNodeInitializer::start()
{
    try
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
    catch (std::exception const& e)
    {
        std::cout << "start bcos-node failed for " << boost::diagnostic_information(e);
        exit(-1);
    }
}

void LocalNodeInitializer::stop()
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