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
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-task/Wait.h"
#include "libinitializer/ProtocolInitializer.h"
#include <bcos-framework/consensus/ConsensusInterface.h>
#include <bcos-framework/gateway/GatewayInterface.h>
#include <bcos-framework/gateway/GroupNodeInfo.h>
#include <bcos-framework/sync/BlockSyncInterface.h>
#include <bcos-framework/txpool/TxPoolInterface.h>
#include <bcos-front/FrontServiceFactory.h>
#include <bcos-tars-protocol/tars/LightNode.h>
#include <fisco-bcos-tars-service/Common/TarsUtils.h>
#include <utility>

using namespace bcos;
using namespace bcos::initializer;
using namespace bcos::front;

FrontServiceInitializer::FrontServiceInitializer(bcos::tool::NodeConfig::Ptr _nodeConfig,
    bcos::initializer::ProtocolInitializer::Ptr _protocolInitializer,
    bcos::gateway::GatewayInterface::Ptr _gateWay)
  : m_nodeConfig(std::move(_nodeConfig)),
    m_protocolInitializer(std::move(_protocolInitializer)),
    m_gateWay(std::move(_gateWay))
{
    auto frontServiceFactory = std::make_shared<FrontServiceFactory>();
    frontServiceFactory->setGatewayInterface(m_gateWay);

    m_front = frontServiceFactory->buildFrontService(
        m_nodeConfig->groupId(), m_protocolInitializer->keyPair()->publicKey());
}

void FrontServiceInitializer::start()
{
    if (m_running)
    {
        FRONTSERVICE_LOG(INFO) << LOG_DESC("The front service has already been started");
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
        FRONTSERVICE_LOG(INFO) << LOG_DESC("The front service has already been stopped");
        return;
    }
    FRONTSERVICE_LOG(INFO) << LOG_DESC("Stop the front service");
    m_running = false;
    m_front->stop();
}

void FrontServiceInitializer::init(bcos::consensus::ConsensusInterface::Ptr _pbft,
    bcos::sync::BlockSyncInterface::Ptr _blockSync, bcos::txpool::TxPoolInterface::Ptr _txpool)
{
    initMsgHandlers(std::move(_pbft), std::move(_blockSync), std::move(_txpool));
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
                nullptr, _id, std::move(_nodeID), _data, [](const bcos::Error::Ptr& _error) {
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
    //
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

    // register the message dispatcher for the consensus txs sync module
    m_front->registerModuleMessageDispatcher(bcos::protocol::ModuleID::ConsTxsSync,
        [_txpool](
            bcos::crypto::NodeIDPtr _nodeID, std::string const& _id, bcos::bytesConstRef _data) {
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

    // register the groupNodeInfo notification to the frontService
    // Note: since txpool/blocksync/pbft are in the same module, they can share the same
    // GroupNodeInfoNotification
    m_front->registerGroupNodeInfoNotification(bcos::protocol::ModuleID::TxsSync,
        [this, _txpool, _blockSync, _pbft](bcos::gateway::GroupNodeInfo::Ptr _groupNodeInfo,
            bcos::front::ReceiveMsgFunc _receiveMsgCallback) {
            auto const& nodeIDList = _groupNodeInfo->nodeIDList();
            bcos::crypto::NodeIDSet nodeIdSet;
            for (auto const& nodeID : nodeIDList)
            {
                auto nodeIDPtr = keyFactory()->createKey(fromHex(nodeID));
                if (!nodeIDPtr)
                {
                    continue;
                }
                nodeIdSet.insert(nodeIDPtr);
            }
            _txpool->notifyConnectedNodes(nodeIdSet, _receiveMsgCallback);
            _blockSync->notifyConnectedNodes(nodeIdSet, _receiveMsgCallback);
            _pbft->notifyConnectedNodes(nodeIdSet, _receiveMsgCallback);
            FRONTSERVICE_LOG(INFO)
                << LOG_DESC("notifyGroupNodeInfo") << LOG_KV("connectedNodeSize", nodeIdSet.size());
        });
    FRONTSERVICE_LOG(INFO) << LOG_DESC("registerGroupNodeInfoNotification success");

    // TXPOOL onPushTransaction
    m_front->registerModuleMessageDispatcher(protocol::SYNC_PUSH_TRANSACTION,
        [this, txpool = _txpool](bcos::crypto::NodeIDPtr const& nodeID,
            const std::string& messageID, bytesConstRef data) {
            if (!txpool->existsInGroup(nodeID)) [[unlikely]]
            {
                if (c_fileLogLevel == TRACE) [[unlikely]]
                {
                    TXPOOL_LOG(TRACE)
                        << "Receive transaction for push from p2p, but the node is not in the group"
                        << LOG_KV("nodeID", nodeID->shortHex()) << LOG_KV("messageID", messageID);
                }
                return;
            }
            auto transaction =
                m_protocolInitializer->blockFactory()->transactionFactory()->createTransaction(
                    data, false);
            if (c_fileLogLevel == TRACE) [[unlikely]]
            {
                TXPOOL_LOG(TRACE) << "Receive push transaction"
                                  << LOG_KV("nodeID", nodeID->shortHex())
                                  << LOG_KV("tx", transaction ? transaction->hash().hex() : "")
                                  << LOG_KV("messageID", messageID);
            }
            transaction->forceSender({});  // must clear sender here for future verify
            task::wait(
                [](decltype(txpool) txpool, decltype(transaction) transaction) -> task::Task<void> {
                    try
                    {
                        [[maybe_unused]] auto submitResult =
                            co_await txpool->submitTransaction(std::move(transaction));
                    }
                    catch (std::exception& e)
                    {
                        TXPOOL_LOG(DEBUG) << "Submit transaction failed from p2p. "
                                          << boost::diagnostic_information(e);
                    }
                }(txpool, std::move(transaction)));
        });

    m_front->registerModuleMessageDispatcher(protocol::TREE_PUSH_TRANSACTION,
        [this, txpool = _txpool](bcos::crypto::NodeIDPtr const& nodeID,
            const std::string& messageID, bytesConstRef data) {
            auto transaction =
                m_protocolInitializer->blockFactory()->transactionFactory()->createTransaction(
                    data, false);
            if (c_fileLogLevel == TRACE) [[unlikely]]
            {
                TXPOOL_LOG(TRACE) << "Receive tree push transaction"
                                  << LOG_KV("nodeID", nodeID->shortHex())
                                  << LOG_KV("tx", transaction ? transaction->hash().hex() : "")
                                  << LOG_KV("messageID", messageID);
            }
            if (!txpool->existsInGroup(nodeID))
            {
                if (c_fileLogLevel == TRACE) [[unlikely]]
                {
                    TXPOOL_LOG(TRACE)
                        << "Receive transaction for push from p2p, but the node is not in the group"
                        << LOG_KV("nodeID", nodeID->shortHex()) << LOG_KV("messageID", messageID);
                }
                return;
            }
            task::wait([](decltype(txpool) txpool, decltype(transaction) transaction,
                           decltype(data) data, decltype(nodeID) nodeID) -> task::Task<void> {
                try
                {
                    bytes buffer(data.begin(), data.end());
                    co_await txpool->broadcastTransactionBufferByTree(
                        bcos::ref(buffer), false, nodeID);
                    [[maybe_unused]] auto submitResult =
                        co_await txpool->submitTransaction(std::move(transaction));
                }
                catch (std::exception& e)
                {
                    TXPOOL_LOG(DEBUG) << "Submit transaction failed from p2p. "
                                      << boost::diagnostic_information(e);
                }
            }(txpool, std::move(transaction), data, nodeID));
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
