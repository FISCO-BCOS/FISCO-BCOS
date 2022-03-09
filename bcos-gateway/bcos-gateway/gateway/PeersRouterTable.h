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
#include "FrontServiceInfo.h"
#include "GatewayStatus.h"
#include <bcos-crypto/interfaces/crypto/KeyFactory.h>
#include <bcos-crypto/interfaces/crypto/KeyInterface.h>
#include <bcos-framework/interfaces/gateway/GroupNodeInfo.h>
#include <bcos-gateway/Common.h>
#include <bcos-gateway/libp2p/P2PInterface.h>
#include <bcos-gateway/protocol/GatewayNodeStatus.h>
#include <memory>

namespace bcos
{
namespace gateway
{
class PeersRouterTable
{
public:
    using Ptr = std::shared_ptr<PeersRouterTable>;
    PeersRouterTable(std::string _uuid, bcos::crypto::KeyFactory::Ptr _keyFactory,
        P2PInterface::Ptr _p2pInterface)
      : m_uuid(_uuid),
        m_keyFactory(_keyFactory),
        m_p2pInterface(_p2pInterface),
        m_gatewayStatusFactory(std::make_shared<GatewayStatusFactory>())
    {}
    virtual ~PeersRouterTable() {}

    bcos::crypto::NodeIDs getGroupNodeIDList(const std::string& _groupID) const;
    std::set<P2pID> queryP2pIDs(const std::string& _groupID, const std::string& _nodeID) const;
    std::set<P2pID> queryP2pIDsByGroupID(const std::string& _groupID) const;
    void removeP2PID(const P2pID& _p2pID);

    void updatePeerStatus(P2pID const& _p2pID, GatewayNodeStatus::Ptr _gatewayNodeStatus);

    using Group2NodeIDListType = std::map<std::string, std::set<std::string>>;
    Group2NodeIDListType peersNodeIDList(P2pID const& _p2pNodeID) const;

    void asyncBroadcastMsg(uint16_t _type, std::string const& _group, P2PMessage::Ptr _msg);

protected:
    void batchInsertNodeList(
        P2pID const& _p2pNodeID, std::vector<GroupNodeInfo::Ptr> const& _nodeList);
    void updatePeerNodeList(P2pID const& _p2pNodeID, GatewayNodeStatus::Ptr _status);

    void removeP2PIDFromGroupNodeList(P2pID const& _p2pID);
    void removePeerStatus(P2pID const& _p2pNodeID);

    void updateGatewayInfo(P2pID const& _p2pNodeID, GatewayNodeStatus::Ptr _status);
    void removeNodeFromGatewayInfo(P2pID const& _p2pID);
    GatewayStatus::Ptr gatewayInfo(std::string const& _uuid);

private:
    std::string m_uuid;
    bcos::crypto::KeyFactory::Ptr m_keyFactory;
    P2PInterface::Ptr m_p2pInterface;
    // used for peer-to-peer router
    // groupID => NodeID => set<P2pID>
    std::map<std::string, std::map<std::string, std::set<P2pID>>> m_groupNodeList;
    mutable SharedMutex x_groupNodeList;

    // the nodeIDList infos of the peers
    // p2pNodeID => groupID => nodeIDList
    std::map<P2pID, GatewayNodeStatus::Ptr> m_peersStatus;
    mutable SharedMutex x_peersStatus;

    GatewayStatusFactory::Ptr m_gatewayStatusFactory;
    // uuid => gatewayInfo
    std::map<std::string, GatewayStatus::Ptr> m_gatewayInfos;
    mutable SharedMutex x_gatewayInfos;
};
}  // namespace gateway
}  // namespace bcos