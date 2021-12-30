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
#include <bcos-framework/interfaces/crypto/KeyFactory.h>
#include <bcos-framework/libutilities/Timer.h>
#include <bcos-gateway/Common.h>
#include <bcos-gateway/libnetwork/Common.h>
#include <bcos-gateway/libp2p/P2PInterface.h>
#include <bcos-gateway/libp2p/P2PSession.h>
namespace bcos
{
namespace gateway
{
class GatewayNodeManager
{
public:
    using Ptr = std::shared_ptr<GatewayNodeManager>;
    GatewayNodeManager(P2pID const& _nodeID, std::shared_ptr<bcos::crypto::KeyFactory> _keyFactory,
        P2PInterface::Ptr _p2pInterface);
    virtual void start() { m_timer->start(); }
    virtual void stop()
    {
        if (m_p2pInterface)
        {
            m_p2pInterface->eraseHandlerByMsgType(MessageType::SyncNodeSeq);
            m_p2pInterface->eraseHandlerByMsgType(MessageType::RequestNodeIDs);
            m_p2pInterface->eraseHandlerByMsgType(MessageType::ResponseNodeIDs);
        }
        if (m_timer)
        {
            m_timer->stop();
        }
    }

    virtual ~GatewayNodeManager() {}

    void updateNodeIDs(const P2pID& _p2pID, uint32_t _seq,
        const std::map<std::string, std::set<std::string>>& _nodeIDsMap);

    void onRemoveNodeIDs(const P2pID& _p2pID);
    void removeNodeIDsByP2PID(const std::string& _p2pID);

    bool queryP2pIDs(
        const std::string& _groupID, const std::string& _nodeID, std::set<P2pID>& _p2pIDs);
    bool queryP2pIDsByGroupID(const std::string& _groupID, std::set<P2pID>& _p2pIDs);
    bool queryNodeIDsByGroupID(const std::string& _groupID, bcos::crypto::NodeIDs& _nodeIDs);

    std::shared_ptr<bcos::crypto::KeyFactory> keyFactory() { return m_keyFactory; }

    // Note: copy for thread-safe
    std::map<std::string, std::set<std::string>> nodeIDInfo(std::string const& _p2pNodeID);

    virtual bool registerNode(const std::string& _groupID, bcos::crypto::NodeIDPtr _nodeID,
        bcos::front::FrontServiceInterface::Ptr _frontService)
    {
        auto ret = m_localRouterTable->insertNode(_groupID, _nodeID, _frontService);
        if (ret)
        {
            increaseSeq();
        }
        return ret;
    }

    virtual bool unregisterNode(const std::string& _groupID, bcos::crypto::NodeIDPtr _nodeID)
    {
        auto ret = m_localRouterTable->removeNode(_groupID, _nodeID);
        if (ret)
        {
            increaseSeq();
        }
        return ret;
    }

    // for multi-group support
    virtual void updateFrontServiceInfo(bcos::group::GroupInfo::Ptr) {}
    LocalRouterTable::Ptr localRouterTable() { return m_localRouterTable; }

protected:
    // for ut
    GatewayNodeManager(std::shared_ptr<bcos::crypto::KeyFactory> _keyFactory)
      : m_keyFactory(_keyFactory),
        m_localRouterTable(std::make_shared<LocalRouterTable>(_keyFactory))
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
        NetworkException const& _e, P2PSession::Ptr _session, std::shared_ptr<P2PMessage> _msg);
    virtual void onRequestNodeIDs(
        NetworkException const& _e, P2PSession::Ptr _session, std::shared_ptr<P2PMessage> _msg);
    virtual void onResponseNodeIDs(
        NetworkException const& _e, P2PSession::Ptr _session, std::shared_ptr<P2PMessage> _msg);
    virtual bool generateNodeInfo(std::string& _nodeInfo);

    bool parseReceivedJson(const std::string& _json, uint32_t& statusSeq,
        std::map<std::string, std::set<std::string>>& nodeIDsMap);

    virtual void updateNodeInfo(const P2pID& _p2pID, const std::string& _nodeIDsJson);

    void updateNodeIDInfo(std::string const& _p2pNodeID,
        std::map<std::string, std::set<std::string>> const& _nodeIDList);
    void removeNodeIDInfo(std::string const& _p2pNodeID);

    virtual void syncLatestNodeIDList();

protected:
    P2pID m_p2pNodeID;
    std::shared_ptr<bcos::crypto::KeyFactory> m_keyFactory;
    P2PInterface::Ptr m_p2pInterface;
    // statusSeq
    std::atomic<uint32_t> m_statusSeq{1};

    LocalRouterTable::Ptr m_localRouterTable;

    // lock m_peerGatewayNodes
    mutable std::mutex x_peerGatewayNodes;
    // groupID => NodeID => set<P2pID>
    std::map<std::string, std::map<std::string, std::set<P2pID>>> m_peerGatewayNodes;
    // P2pID => statusSeq
    std::map<std::string, uint32_t> m_p2pID2Seq;

    // the nodeIDList infos of the peers
    // p2pNodeID->groupID->nodeIDList
    std::map<std::string, std::map<std::string, std::set<std::string>>> m_nodeIDInfo;
    SharedMutex x_nodeIDInfo;

    unsigned const SEQ_SYNC_PERIOD = 3000;
    std::shared_ptr<Timer> m_timer;
};
}  // namespace gateway
}  // namespace bcos
