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
#include <bcos-framework/interfaces/crypto/KeyFactory.h>
#include <bcos-framework/interfaces/front/FrontServiceInterface.h>
#include <bcos-framework/interfaces/gateway/GatewayInterface.h>
#include <bcos-gateway/Common.h>
#include <bcos-gateway/libnetwork/Common.h>
#include <bcos-tars-protocol/client/FrontServiceClient.h>
namespace bcos
{
namespace gateway
{
class FrontServiceInfo
{
public:
    using Ptr = std::shared_ptr<FrontServiceInfo>;
    FrontServiceInfo(bcos::front::FrontServiceInterface::Ptr _frontService,
        bcostars::FrontServicePrx _frontServicePrx)
      : m_frontService(_frontService), m_frontServicePrx(_frontServicePrx)
    {}


    bcos::front::FrontServiceInterface::Ptr frontService() { return m_frontService; }
    bcostars::FrontServicePrx frontServicePrx() { return m_frontServicePrx; }

    bool unreachable()
    {
        if (!m_frontServicePrx)
        {
            return false;
        }
        vector<EndpointInfo> activeEndPoints;
        vector<EndpointInfo> nactiveEndPoints;
        m_frontServicePrx->tars_endpointsAll(activeEndPoints, nactiveEndPoints);
        return (activeEndPoints.size() == 0);
    }

private:
    bcos::front::FrontServiceInterface::Ptr m_frontService;
    bcostars::FrontServicePrx m_frontServicePrx;
};

class GatewayNodeManager
{
public:
    using Ptr = std::shared_ptr<GatewayNodeManager>;
    GatewayNodeManager(P2pID const& _nodeID, std::shared_ptr<bcos::crypto::KeyFactory> _keyFactory)
      : m_p2pNodeID(_nodeID), m_keyFactory(_keyFactory)
    {}

    virtual void start() {}
    virtual void stop() {}

    virtual ~GatewayNodeManager() {}

    uint32_t statusSeq() { return m_statusSeq; }
    uint32_t increaseSeq()
    {
        uint32_t statusSeq = ++m_statusSeq;
        return statusSeq;
    }

    bool parseReceivedJson(const std::string& _json, uint32_t& statusSeq,
        std::unordered_map<std::string, std::set<std::string>>& nodeIDsMap);
    void updateNodeIDs(const P2pID& _p2pID, uint32_t _seq,
        const std::unordered_map<std::string, std::set<std::string>>& _nodeIDsMap);

    void onReceiveStatusSeq(const P2pID& _p2pID, uint32_t _statusSeq, bool& _statusSeqChanged);
    void onReceiveNodeIDs(const P2pID& _p2pID, const std::string& _nodeIDsJson);
    void onRequestNodeIDs(std::string& _nodeIDsJson);
    void onRemoveNodeIDs(const P2pID& _p2pID);
    void removeNodeIDsByP2PID(const std::string& _p2pID);

    bool queryP2pIDs(
        const std::string& _groupID, const std::string& _nodeID, std::set<P2pID>& _p2pIDs);
    bool queryP2pIDsByGroupID(const std::string& _groupID, std::set<P2pID>& _p2pIDs);
    bool queryNodeIDsByGroupID(const std::string& _groupID, bcos::crypto::NodeIDs& _nodeIDs);

    void showAllPeerGatewayNodeIDs();
    void notifyNodeIDs2FrontService();

    bcos::front::FrontServiceInterface::Ptr queryFrontServiceInterfaceByGroupIDAndNodeID(
        const std::string& _groupID, bcos::crypto::NodeIDPtr _nodeID);
    std::set<bcos::front::FrontServiceInterface::Ptr> queryFrontServiceInterfaceByGroupID(
        const std::string& _groupID);
    /**
     * @brief: register FrontService
     * @param _groupID: groupID
     * @param _nodeID: nodeID
     * @param _frontServiceInterface: FrontService
     * @return bool
     */
    bool registerFrontService(const std::string& _groupID, bcos::crypto::NodeIDPtr _nodeID,
        bcos::front::FrontServiceInterface::Ptr _frontServiceInterface);
    /**
     * @brief: unregister FrontService
     * @param _groupID: groupID
     * @param _nodeID: nodeID
     * @return bool
     */
    bool unregisterFrontService(const std::string& _groupID, bcos::crypto::NodeIDPtr _nodeID);

    const std::unordered_map<std::string, std::unordered_map<std::string, FrontServiceInfo::Ptr>>&
    frontServiceInfos() const
    {
        return m_frontServiceInfos;
    }
    std::shared_ptr<bcos::crypto::KeyFactory> keyFactory() { return m_keyFactory; }
    FrontServiceInfo::Ptr queryLocalNodes(std::string const& _groupID, std::string const& _nodeID);
    std::unordered_map<std::string, FrontServiceInfo::Ptr> groupFrontServices(
        std::string const& _groupID);

    // Note: copy for thread-safe
    std::unordered_map<std::string, std::set<std::string>> nodeIDInfo(
        std::string const& _p2pNodeID);
    std::unordered_map<std::string, std::set<std::string>> getLocalNodeIDInfo();

    // for multi-group support
    virtual void updateFrontServiceInfo(bcos::group::GroupInfo::Ptr) {}
    void queryLocalNodeIDsByGroup(const std::string& _groupID, bcos::crypto::NodeIDs& _nodeIDs);

protected:
    void updateNodeIDInfo(std::string const& _p2pNodeID,
        std::unordered_map<std::string, std::set<std::string>> const& _nodeIDList);
    void removeNodeIDInfo(std::string const& _p2pNodeID);

protected:
    P2pID m_p2pNodeID;
    std::shared_ptr<bcos::crypto::KeyFactory> m_keyFactory;
    // statusSeq
    std::atomic<uint32_t> m_statusSeq{1};
    // lock m_peerGatewayNodes
    mutable std::mutex x_peerGatewayNodes;
    // groupID => NodeID => set<P2pID>
    std::unordered_map<std::string, std::unordered_map<std::string, std::set<P2pID>>>
        m_peerGatewayNodes;
    // P2pID => statusSeq
    std::unordered_map<std::string, uint32_t> m_p2pID2Seq;
    // lock m_groupID2FrontServiceInterface
    mutable SharedMutex x_frontServiceInfos;
    // groupID => nodeID => FrontServiceInterface
    std::unordered_map<std::string, std::unordered_map<std::string, FrontServiceInfo::Ptr>>
        m_frontServiceInfos;

    // the groupNodeID info
    // p2pNodeID->groupID->nodeIDList
    std::map<std::string, std::unordered_map<std::string, std::set<std::string>>> m_nodeIDInfo;
    SharedMutex x_nodeIDInfo;
};
}  // namespace gateway
}  // namespace bcos
