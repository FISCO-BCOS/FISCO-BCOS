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
#include "bcos-crypto/interfaces/crypto/KeyFactory.h"
#include "bcos-gateway/libnetwork/Common.h"
#include "bcos-gateway/libp2p/P2PSession.h"
#include "bcos-gateway/libp2p/Service.h"
#include "bcos-gateway/protocol/GatewayNodeStatus.h"
#include "bcos-utilities/Timer.h"
#include <oneapi/tbb/concurrent_hash_map.h>

namespace bcos::gateway
{
class GatewayNodeManager
{
public:
    using Ptr = std::shared_ptr<GatewayNodeManager>;
    // _enableNodeAliveDetection selects the former ProGatewayNodeManager behaviour (pro/tars
    // deployment without a failover entry point): a periodic timer erases the unreachable nodes
    // from the local router table, since tars needs at least 1min to refresh endpoint info.
    GatewayNodeManager(std::string const& _uuid, P2pID const& _nodeID,
        std::shared_ptr<bcos::crypto::KeyFactory> _keyFactory, Service::Ptr _p2pInterface,
        boost::asio::io_context& _ioContext, bool _enableNodeAliveDetection = false);
    ~GatewayNodeManager();

    void start();
    void stop();

    void onRemoveNodeIDs(const P2pID& _p2pID);

    GroupNodeInfo::Ptr getGroupNodeInfoList(const std::string& _groupID);

    bool registerNode(const std::string& _groupID, bcos::crypto::NodeIDPtr _nodeID,
        bcos::protocol::NodeType _nodeType, bcos::front::FrontService::Ptr _frontService,
        bcos::protocol::ProtocolInfo::ConstPtr _protocolInfo);
    bool unregisterNode(const std::string& _groupID, std::string const& _nodeID);
    // for multi-group support
    bool updateFrontServiceInfo(bcos::group::GroupInfo::Ptr _groupInfo);

    LocalRouterTable::Ptr localRouterTable();
    PeersRouterTable::Ptr peersRouterTable();
    std::shared_ptr<bcos::crypto::KeyFactory> keyFactory();

    std::map<std::string, std::map<std::string, uint32_t>> peersNodeIDList(
        std::string const& _p2pNodeID);

protected:
    // for ut
    GatewayNodeManager(std::string const& _uuid,
        std::shared_ptr<bcos::crypto::KeyFactory> _keyFactory, Service::Ptr _p2pInterface);

    uint32_t increaseSeq();
    bool statusChanged(std::string const& _p2pNodeID, uint32_t _seq);
    uint32_t statusSeq();
    // Note: must broadcast the status seq periodically ensure that the seq can be synced to
    // restarted or re-connected nodes
    void broadcastStatusSeq();

    void onReceiveStatusSeq(
        NetworkException const& _e, P2PSession::Ptr _session, std::shared_ptr<P2PMessage> _msg);
    void onRequestNodeStatus(
        NetworkException const& _e, P2PSession::Ptr _session, std::shared_ptr<P2PMessage> _msg);
    void onReceiveNodeStatus(
        NetworkException const& _e, P2PSession::Ptr _session, std::shared_ptr<P2PMessage> _msg);
    bytesPointer generateNodeStatus();
    void syncLatestNodeIDList();

    void updatePeerStatus(std::string const& _p2pID, GatewayNodeStatus::Ptr _status);

private:
    // node-alive detection (the former ProGatewayNodeManager)
    void detectNodeAlive();

protected:
    P2pID m_p2pNodeID;
    std::string m_uuid;
    std::shared_ptr<bcos::crypto::KeyFactory> m_keyFactory;
    Service::Ptr m_p2pInterface;
    // statusSeq
    std::atomic<uint32_t> m_statusSeq{1};
    tbb::concurrent_hash_map<std::string, uint32_t> m_p2pID2Seq;

    LocalRouterTable::Ptr m_localRouterTable;
    PeersRouterTable::Ptr m_peersRouterTable;

    unsigned const SEQ_SYNC_PERIOD = 1000;
    std::shared_ptr<Timer> m_timer;
    // FIB-186 (vector D): coalesce disconnect-driven node-list syncs. onRemoveNodeIDs syncs inline
    // only on the first drop of a burst (the clean->dirty transition) and otherwise sets this flag;
    // the seqSync timer flushes it at most once per SEQ_SYNC_PERIOD. So the front is refreshed
    // promptly on the first drop, while a persistent bulk-disconnect is bounded to ~one sync per
    // period instead of one full node-list broadcast to every front per dropped session.
    std::atomic_bool m_nodeIDListDirty{false};

    // node-alive detector, only created when _enableNodeAliveDetection (pro/tars mode)
    std::shared_ptr<Timer> m_nodeAliveDetector;
    // Note: since tars need at-least 1min to update the endpoint info, we schedule detectNodeAlive
    // every 1min
    uint64_t c_tarsAdminRefreshTimeInterval = 30 * 1000;
};
}  // namespace bcos::gateway
