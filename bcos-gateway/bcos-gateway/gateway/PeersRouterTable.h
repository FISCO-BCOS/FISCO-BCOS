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
 * @file PeersRouterTable.h
 * @author: octopus
 * @date 2021-12-29
 */
#pragma once
#include "GatewayStatus.h"
#include "bcos-crypto/interfaces/crypto/KeyFactory.h"
#include "bcos-framework/gateway/GroupNodeInfo.h"
#include "bcos-framework/protocol/ProtocolInfo.h"
#include "bcos-gateway/libp2p/P2PInterface.h"
#include "bcos-gateway/libp2p/P2PMessageV2.h"
#include "bcos-gateway/protocol/GatewayNodeStatus.h"
#include "bcos-task/Task.h"
#include <oneapi/tbb/concurrent_unordered_map.h>
#include <memory>
#include <utility>

namespace bcos::gateway
{
class PeersRouterTable
{
public:
    using Ptr = std::shared_ptr<PeersRouterTable>;
    PeersRouterTable(std::string _uuid, bcos::crypto::KeyFactory::Ptr _keyFactory,
        P2PInterface::Ptr _p2pInterface)
      : m_uuid(std::move(_uuid)),
        m_keyFactory(std::move(_keyFactory)),
        m_p2pInterface(std::move(_p2pInterface)),
        m_gatewayStatusFactory(std::make_shared<GatewayStatusFactory>())
    {}
    virtual ~PeersRouterTable() = default;

    void getGroupNodeInfoList(GroupNodeInfo::Ptr _groupInfo, const std::string& _groupID) const;
    std::set<P2pID> queryP2pIDs(const std::string& _groupID, const std::string& _nodeID) const;
    std::set<P2pID> queryP2pIDsByGroupID(const std::string& _groupID) const;
    void removeP2PID(const P2pID& _p2pID);

    void updatePeerStatus(P2pID const& _p2pID, GatewayNodeStatus::Ptr _gatewayNodeStatus);

    using Group2NodeIDListType = std::map<std::string, std::map<std::string, uint32_t>>;
    Group2NodeIDListType peersNodeIDList(P2pID const& _p2pNodeID) const;

    task::Task<void> broadcastMessage(uint16_t type, std::string_view group, uint16_t moduleID,
        const P2PMessageV2& message,
        ::ranges::any_view<bytesConstRef, ::ranges::category::forward> payloads);

    std::set<P2pID> getAllPeers() const;
    GatewayStatus::Ptr gatewayInfo(std::string const& _uuid);

protected:
    void batchInsertNodeList(
        P2pID const& _p2pNodeID, std::vector<GroupNodeInfo::Ptr> const& _nodeList);
    void updatePeerNodeList(P2pID const& _p2pNodeID, GatewayNodeStatus::Ptr _status);

    void removeP2PIDFromGroupNodeList(P2pID const& _p2pID);
    void removePeerStatus(P2pID const& _p2pNodeID);

    void updateGatewayInfo(P2pID const& _p2pNodeID, GatewayNodeStatus::Ptr _status);
    void removeNodeFromGatewayInfo(P2pID const& _p2pID);

private:
    std::string m_uuid;
    bcos::crypto::KeyFactory::Ptr m_keyFactory;
    P2PInterface::Ptr m_p2pInterface;
    // used for peer-to-peer router
    // groupID => NodeID => set<P2pID>
    std::map<std::string, std::map<std::string, std::set<P2pID>, std::less<>>, std::less<>>
        m_groupNodeList;
    // FIB-186 (vector D): reverse index p2pID => the (groupID, nodeID) entries that p2pID appears
    // under in m_groupNodeList. removeP2PIDFromGroupNodeList used to scan the whole forward map
    // (O(groups x nodes x p2pIDs)) under x_groupNodeList's WriteLock; under a persistent
    // bulk-disconnect the teardown thread reacquires that WriteLock once per dropped peer and
    // blocks queryP2pIDs -- the ReadLock on the unicast routing hot path
    // (Gateway::asyncSendMessageByNodeID). This index lets removal touch only the entries a peer
    // actually holds, bounding the WriteLock hold time to O(K). Maintained under x_groupNodeList
    // together with m_groupNodeList (same lock, so the two are always consistent).
    //
    // Each entry stores POINTERS to the (groupID, nodeID) key strings owned by m_groupNodeList's
    // nodes, not copies, to avoid duplicating every groupID/nodeID string.
    // Lifetime invariant: std::map/std::set keep a node's key object at a stable address until that
    // element is erased, and removeP2PIDFromGroupNodeList erases a (group)/(node) element only when
    // its p2pID set empties -- i.e. when no peer (including this one) still references it -- so a
    // pointer stored here always outlives the reverse entry holding it. This REQUIRES
    // m_groupNodeList to remain a node-based container: do NOT switch it to a flat/vector-backed
    // map whose keys move on insert/rehash, or these pointers dangle.
    std::map<P2pID, std::set<std::pair<const std::string*, const std::string*>>> m_p2pID2GroupNodes;
    std::map<std::string, bcos::protocol::ProtocolInfo::ConstPtr> m_nodeProtocolInfo;
    mutable SharedMutex x_groupNodeList;

    // the nodeIDList infos of the peers
    // p2pNodeID => GatewayNodeStatus
    std::map<P2pID, GatewayNodeStatus::Ptr> m_peersStatus;
    mutable SharedMutex x_peersStatus;

    GatewayStatusFactory::Ptr m_gatewayStatusFactory;
    tbb::concurrent_unordered_map<std::string, GatewayStatus::Ptr> m_gatewayInfos;
};
}  // namespace bcos::gateway
