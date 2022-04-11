/*
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
 * @file GatewayNodeManager.h
 * @author: octopus
 * @date 2021-05-13
 */

#pragma once
#include "LocalRouterTable.h"
#include "PeersRouterTable.h"
#include <bcos-boostssl/websocket/WsSession.h>
#include <bcos-crypto/interfaces/crypto/KeyFactory.h>
#include <bcos-gateway/Common.h>
#include <bcos-gateway/libp2p/P2PInterface.h>
#include <bcos-gateway/libp2p/P2PSession.h>
#include <bcos-gateway/protocol/GatewayNodeStatus.h>
#include <bcos-utilities/Timer.h>
namespace bcos
{
namespace gateway
{
class GatewayNodeManager
{
public:
    using Ptr = std::shared_ptr<GatewayNodeManager>;
    GatewayNodeManager(std::string const& _uuid, P2pID const& _nodeID,
        std::shared_ptr<bcos::crypto::KeyFactory> _keyFactory, P2PInterface::Ptr _p2pInterface);
    virtual ~GatewayNodeManager() {}

    virtual void start() { m_timer->start(); }
    virtual void stop();

    void onRemoveNodeIDs(const P2pID& _p2pID);

    GroupNodeInfo::Ptr getGroupNodeInfoList(const std::string& _groupID);

    virtual bool registerNode(const std::string& _groupID, bcos::crypto::NodeIDPtr _nodeID,
        bcos::protocol::NodeType _nodeType, bcos::front::FrontServiceInterface::Ptr _frontService,
        bcos::protocol::ProtocolInfo::ConstPtr _protocolInfo);
    virtual bool unregisterNode(const std::string& _groupID, bcos::crypto::NodeIDPtr _nodeID);
    // for multi-group support
    virtual void updateFrontServiceInfo(bcos::group::GroupInfo::Ptr) {}

    LocalRouterTable::Ptr localRouterTable() { return m_localRouterTable; }
    PeersRouterTable::Ptr peersRouterTable() { return m_peersRouterTable; }
    std::shared_ptr<bcos::crypto::KeyFactory> keyFactory() { return m_keyFactory; }

    std::map<std::string, std::set<std::string>> peersNodeIDList(std::string const& _p2pNodeID);

protected:
    // for ut
    GatewayNodeManager(std::string const& _uuid,
        std::shared_ptr<bcos::crypto::KeyFactory> _keyFactory, P2PInterface::Ptr _p2pInterface)
      : m_uuid(_uuid),
        m_keyFactory(_keyFactory),
        m_localRouterTable(std::make_shared<LocalRouterTable>(_keyFactory)),
        m_peersRouterTable(std::make_shared<PeersRouterTable>(_uuid, _keyFactory, _p2pInterface)),
        m_gatewayNodeStatusFactory(std::make_shared<GatewayNodeStatusFactory>())
    {}

    uint32_t increaseSeq()
    {
        uint32_t statusSeq = ++m_statusSeq;
        return statusSeq;
    }
    bool statusChanged(std::string const& _p2pNodeID, uint32_t _seq);
    uint32_t statusSeq() { return m_statusSeq; }
    // Note: must broadcast the status seq periodically ensure that the seq can be synced to
    // restarted or re-connected nodes
    virtual void broadcastStatusSeq();

    virtual void onReceiveStatusSeq(
        std::shared_ptr<boostssl::MessageFace> _msg, std::shared_ptr<P2PSession> _p2pSession);
    virtual void onRequestNodeStatus(
        std::shared_ptr<boostssl::MessageFace> _msg, std::shared_ptr<P2PSession> _p2pSession);
    virtual void onReceiveNodeStatus(
        std::shared_ptr<boostssl::MessageFace> _msg, std::shared_ptr<P2PSession> _p2pSession);
    virtual bytesPointer generateNodeStatus();
    virtual void syncLatestNodeIDList();

    virtual void updatePeerStatus(std::string const& _p2pID, GatewayNodeStatus::Ptr _status);

protected:
    P2pID m_p2pNodeID;
    std::string m_uuid;
    std::shared_ptr<bcos::crypto::KeyFactory> m_keyFactory;
    P2PInterface::Ptr m_p2pInterface;
    // statusSeq
    std::atomic<uint32_t> m_statusSeq{1};
    // P2pID => statusSeq
    std::map<std::string, uint32_t> m_p2pID2Seq;
    mutable SharedMutex x_p2pID2Seq;

    LocalRouterTable::Ptr m_localRouterTable;
    PeersRouterTable::Ptr m_peersRouterTable;

    unsigned const SEQ_SYNC_PERIOD = 3000;
    std::shared_ptr<Timer> m_timer;

    GatewayNodeStatusFactory::Ptr m_gatewayNodeStatusFactory;
};
}  // namespace gateway
}  // namespace bcos
