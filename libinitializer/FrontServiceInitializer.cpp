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
 * @brief initializer for the the front module
 * @file FrontServiceInitializer.cpp
 * @author: yujiechen
 * @date 2021-06-10
 */
#include "FrontServiceInitializer.h"
#include "Common/TarsUtils.h"
#include "libinitializer/ProtocolInitializer.h"
#include <bcos-framework/interfaces/consensus/ConsensusInterface.h>
#include <bcos-framework/interfaces/gateway/GatewayInterface.h>
#include <bcos-framework/interfaces/sync/BlockSyncInterface.h>
#include <bcos-framework/interfaces/txpool/TxPoolInterface.h>
#include <bcos-front/FrontServiceFactory.h>

using namespace bcos;
using namespace bcos::initializer;
using namespace bcos::front;

FrontServiceInitializer::FrontServiceInitializer(bcos::tool::NodeConfig::Ptr _nodeConfig,
    bcos::initializer::ProtocolInitializer::Ptr _protocolInitializer,
    bcos::gateway::GatewayInterface::Ptr _gateWay)
  : m_nodeConfig(_nodeConfig), m_protocolInitializer(_protocolInitializer), m_gateWay(_gateWay)
{
    auto frontServiceFactory = std::make_shared<FrontServiceFactory>();
    frontServiceFactory->setGatewayInterface(m_gateWay);

    auto threadPool = std::make_shared<ThreadPool>("frontService", 16);
    frontServiceFactory->setThreadPool(threadPool);
    m_front = frontServiceFactory->buildFrontService(
        m_nodeConfig->groupId(), m_protocolInitializer->keyPair()->publicKey());
}

void FrontServiceInitializer::start()
{
    if (m_running)
    {
        FRONTSERVICE_LOG(WARNING) << LOG_DESC("The front service has already been started");
        return;
    }
    FRONTSERVICE_LOG(INFO) << LOG_DESC("Start the front service");
    m_running = true;
    m_front->start();
}
void FrontServiceInitializer::stop()
{
    if (!m_running)
    {
        FRONTSERVICE_LOG(WARNING) << LOG_DESC("The front service has already been stopped");
        return;
    }
    FRONTSERVICE_LOG(INFO) << LOG_DESC("Stop the front service");
    m_running = false;
    m_front->stop();
}

void FrontServiceInitializer::init(bcos::consensus::ConsensusInterface::Ptr _pbft,
    bcos::sync::BlockSyncInterface::Ptr _blockSync, bcos::txpool::TxPoolInterface::Ptr _txpool)
{
    initMsgHandlers(_pbft, _blockSync, _txpool);
}


void FrontServiceInitializer::initMsgHandlers(bcos::consensus::ConsensusInterface::Ptr _pbft,
    bcos::sync::BlockSyncInterface::Ptr _blockSync, bcos::txpool::TxPoolInterface::Ptr _txpool)
{
    // register the message dispatcher handler to the frontService
    // register the message dispatcher for PBFT module
    m_front->registerModuleMessageDispatcher(
        bcos::protocol::ModuleID::PBFT, [_pbft](bcos::crypto::NodeIDPtr _nodeID,
                                            const std::string& _id, bcos::bytesConstRef _data) {
            _pbft->asyncNotifyConsensusMessage(
                nullptr, _id, _nodeID, _data, [](bcos::Error::Ptr _error) {
                    if (_error)
                    {
                        FRONTSERVICE_LOG(WARNING)
                            << LOG_DESC("registerModuleMessageDispatcher failed")
                            << LOG_KV("code", _error->errorCode())
                            << LOG_KV("msg", _error->errorMessage());
                    }
                });
        });
    FRONTSERVICE_LOG(INFO) << LOG_DESC(
        "registerModuleMessageDispatcher for the consensus module success");

    // register the message dispatcher for the txsSync module
    m_front->registerModuleMessageDispatcher(
        bcos::protocol::ModuleID::TxsSync, [_txpool](bcos::crypto::NodeIDPtr _nodeID,
                                               std::string const& _id, bcos::bytesConstRef _data) {
            _txpool->asyncNotifyTxsSyncMessage(
                nullptr, _id, _nodeID, _data, [_id](bcos::Error::Ptr _error) {
                    if (_error)
                    {
                        FRONTSERVICE_LOG(WARNING) << LOG_DESC("asyncNotifyTxsSyncMessage failed")
                                                  << LOG_KV("code", _error->errorCode())
                                                  << LOG_KV("msg", _error->errorMessage());
                    }
                });
        });
    FRONTSERVICE_LOG(INFO) << LOG_DESC(
        "registerModuleMessageDispatcher for the txsSync module success");

    // register the message dispatcher for the block sync module
    m_front->registerModuleMessageDispatcher(bcos::protocol::ModuleID::BlockSync,
        [_blockSync](
            bcos::crypto::NodeIDPtr _nodeID, std::string const& _id, bcos::bytesConstRef _data) {
            _blockSync->asyncNotifyBlockSyncMessage(
                nullptr, _id, _nodeID, _data, [_id, _nodeID](bcos::Error::Ptr _error) {
                    if (_error)
                    {
                        FRONTSERVICE_LOG(WARNING)
                            << LOG_DESC("asyncNotifyBlockSyncMessage failed")
                            << LOG_KV("peer", _nodeID->shortHex()) << LOG_KV("id", _id)
                            << LOG_KV("code", _error->errorCode())
                            << LOG_KV("msg", _error->errorMessage());
                    }
                });
        });
    FRONTSERVICE_LOG(INFO) << LOG_DESC(
        "registerModuleMessageDispatcher for the BlockSync module success");

    // register the GetNodeIDsDispatcher to the frontService
    m_front->registerModuleNodeIDsDispatcher(bcos::protocol::ModuleID::TxsSync,
        [_txpool](std::shared_ptr<const bcos::crypto::NodeIDs> _nodeIDs,
            bcos::front::ReceiveMsgFunc _receiveMsgCallback) {
            auto nodeIdSet = bcos::crypto::NodeIDSet(_nodeIDs->begin(), _nodeIDs->end());
            _txpool->notifyConnectedNodes(nodeIdSet, _receiveMsgCallback);
            FRONTSERVICE_LOG(DEBUG) << LOG_DESC("TxPool: notifyConnectedNodes")
                                    << LOG_KV("connectedNodeSize", nodeIdSet.size());
        });

    FRONTSERVICE_LOG(INFO) << LOG_DESC(
        "registerModuleNodeIDsDispatcher for the TxsSync module success");

    m_front->registerModuleNodeIDsDispatcher(bcos::protocol::ModuleID::BlockSync,
        [_blockSync](std::shared_ptr<const bcos::crypto::NodeIDs> _nodeIDs,
            bcos::front::ReceiveMsgFunc _receiveMsgCallback) {
            auto nodeIdSet = bcos::crypto::NodeIDSet(_nodeIDs->begin(), _nodeIDs->end());
            _blockSync->notifyConnectedNodes(nodeIdSet, _receiveMsgCallback);
            FRONTSERVICE_LOG(DEBUG) << LOG_DESC("BlockSync: notifyConnectedNodes")
                                    << LOG_KV("connectedNodeSize", nodeIdSet.size());
        });

    FRONTSERVICE_LOG(INFO) << LOG_DESC(
        "registerModuleNodeIDsDispatcher for the BlockSync module success");
    m_front->registerModuleNodeIDsDispatcher(bcos::protocol::ModuleID::PBFT,
        [_pbft](std::shared_ptr<const bcos::crypto::NodeIDs> _nodeIDs,
            bcos::front::ReceiveMsgFunc _receiveMsgCallback) {
            auto nodeIdSet = bcos::crypto::NodeIDSet(_nodeIDs->begin(), _nodeIDs->end());
            _pbft->notifyConnectedNodes(nodeIdSet, _receiveMsgCallback);
            FRONTSERVICE_LOG(DEBUG) << LOG_DESC("PBFT: notifyConnectedNodes")
                                    << LOG_KV("connectedNodeSize", nodeIdSet.size());
        });
}

bcos::crypto::KeyFactory::Ptr FrontServiceInitializer::keyFactory()
{
    return m_protocolInitializer->keyFactory();
}
bcos::front::FrontServiceInterface::Ptr FrontServiceInitializer::front()
{
    return m_front;
}