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
 * @file PeersRouterTable.cpp
 * @author: octopus
 * @date 2021-12-29
 */
#include "PeersRouterTable.h"

using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::gateway;
using namespace bcos::crypto;

void PeersRouterTable::getGroupNodeInfoList(
    GroupNodeInfo::Ptr _groupInfo, const std::string& _groupID) const
{
    ReadGuard l(x_groupNodeList);
    if (!m_groupNodeList.count(_groupID))
    {
        return;
    }
    for (auto const& it : m_groupNodeList.at(_groupID))
    {
        auto nodeID = it.first;
        _groupInfo->appendNodeID(nodeID);
        if (m_nodeProtocolInfo.count(nodeID))
        {
            _groupInfo->appendProtocol(m_nodeProtocolInfo.at(nodeID));
        }
    }
}

std::set<P2pID> PeersRouterTable::queryP2pIDs(
    const std::string& _groupID, const std::string& _nodeID) const
{
    ReadGuard l(x_groupNodeList);
    if (!m_groupNodeList.count(_groupID) || !m_groupNodeList.at(_groupID).count(_nodeID))
    {
        return std::set<P2pID>();
    }
    return m_groupNodeList.at(_groupID).at(_nodeID);
}

std::set<P2pID> PeersRouterTable::queryP2pIDsByGroupID(const std::string& _groupID) const
{
    std::set<P2pID> p2pNodeIDList;
    ReadGuard l(x_groupNodeList);
    if (!m_groupNodeList.count(_groupID))
    {
        return p2pNodeIDList;
    }
    for (const auto& it : m_groupNodeList.at(_groupID))
    {
        p2pNodeIDList.insert(it.second.begin(), it.second.end());
    }
    return p2pNodeIDList;
}

void PeersRouterTable::updatePeerStatus(
    P2pID const& _p2pID, GatewayNodeStatus::Ptr _gatewayNodeStatus)
{
    auto const& nodeList = _gatewayNodeStatus->groupNodeInfos();
    ROUTER_LOG(INFO) << LOG_DESC("updatePeerStatus")
                     << LOG_KV("gatewayUUID", _gatewayNodeStatus->uuid())
                     << LOG_KV("nodeList", nodeList.size());
    // remove the old nodeList from the groupNodeList
    removeP2PIDFromGroupNodeList(_p2pID);
    // insert the new nodeList into the  groupNodeList
    batchInsertNodeList(_p2pID, nodeList);
    // update the peers status
    updatePeerNodeList(_p2pID, _gatewayNodeStatus);
    // update the gatewayInfo
    updateGatewayInfo(_p2pID, _gatewayNodeStatus);
}

void PeersRouterTable::batchInsertNodeList(
    P2pID const& _p2pNodeID, std::vector<GroupNodeInfo::Ptr> const& _nodeList)
{
    WriteGuard l(x_groupNodeList);
    for (auto const& it : _nodeList)
    {
        auto groupID = it->groupID();
        auto const& nodeIDList = it->nodeIDList();
        int64_t i = 0;
        for (auto const& nodeID : nodeIDList)
        {
            if (!m_groupNodeList.count(groupID) || !m_groupNodeList.at(groupID).count(nodeID))
            {
                m_groupNodeList[groupID][nodeID] = std::set<P2pID>();
            }
            m_groupNodeList[groupID][nodeID].insert(_p2pNodeID);
            if (it->protocol(i))
            {
                m_nodeProtocolInfo[nodeID] = it->protocol(i);
            }
            i++;
        }
        ROUTER_LOG(INFO) << LOG_DESC("batchInsertNodeList") << LOG_KV("group", it->groupID())
                         << LOG_KV("nodeIDs", it->nodeIDList().size())
                         << LOG_KV("protocols", it->nodeProtocolList().size());
    }
}

void PeersRouterTable::removeP2PID(const P2pID& _p2pID)
{
    ROUTER_LOG(INFO) << LOG_DESC("PeersRouterTable: removeP2PID") << LOG_KV("p2pID", _p2pID);
    // remove p2pID from groupNodeList
    removeP2PIDFromGroupNodeList(_p2pID);
    // remove p2pID from peerStatus
    removePeerStatus(_p2pID);
    // remove p2pID from the gatewayInfo
    removeNodeFromGatewayInfo(_p2pID);
}

void PeersRouterTable::removeP2PIDFromGroupNodeList(const P2pID& _p2pID)
{
    WriteGuard l(x_groupNodeList);
    // remove all nodeIDs info belong to p2pID
    for (auto it = m_groupNodeList.begin(); it != m_groupNodeList.end();)
    {
        for (auto innerIt = it->second.begin(); innerIt != it->second.end();)
        {
            for (auto innerIt2 = innerIt->second.begin(); innerIt2 != innerIt->second.end();)
            {
                if (*innerIt2 == _p2pID)
                {
                    innerIt2 = innerIt->second.erase(innerIt2);
                }
                else
                {
                    ++innerIt2;
                }
            }

            if (innerIt->second.empty())
            {
                innerIt = it->second.erase(innerIt);
            }
            else
            {
                ++innerIt;
            }
        }

        if (it->second.empty())
        {
            it = m_groupNodeList.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void PeersRouterTable::updatePeerNodeList(P2pID const& _p2pNodeID, GatewayNodeStatus::Ptr _status)
{
    WriteGuard l(x_peersStatus);
    m_peersStatus[_p2pNodeID] = _status;
}

void PeersRouterTable::removePeerStatus(P2pID const& _p2pNodeID)
{
    UpgradableGuard l(x_peersStatus);
    if (m_peersStatus.count(_p2pNodeID))
    {
        UpgradeGuard ul(l);
        m_peersStatus.erase(_p2pNodeID);
    }
}

PeersRouterTable::Group2NodeIDListType PeersRouterTable::peersNodeIDList(
    P2pID const& _p2pNodeID) const
{
    ReadGuard l(x_peersStatus);
    PeersRouterTable::Group2NodeIDListType nodeIDList;
    if (!m_peersStatus.count(_p2pNodeID))
    {
        return nodeIDList;
    }
    auto const& groupNodeInfos = m_peersStatus.at(_p2pNodeID)->groupNodeInfos();
    for (auto const& it : groupNodeInfos)
    {
        auto const& groupNodeIDList = it->nodeIDList();
        nodeIDList[it->groupID()] =
            std::set<std::string>(groupNodeIDList.begin(), groupNodeIDList.end());
    }
    return nodeIDList;
}

GatewayStatus::Ptr PeersRouterTable::gatewayInfo(std::string const& _uuid)
{
    ReadGuard l(x_gatewayInfos);
    if (m_gatewayInfos.count(_uuid))
    {
        return m_gatewayInfos.at(_uuid);
    }
    return nullptr;
}

void PeersRouterTable::updateGatewayInfo(P2pID const& _p2pNodeID, GatewayNodeStatus::Ptr _status)
{
    GatewayStatus::Ptr gatewayStatus;
    {
        UpgradableGuard l(x_gatewayInfos);
        if (!m_gatewayInfos.count(_status->uuid()))
        {
            UpgradeGuard ul(l);
            m_gatewayInfos[_status->uuid()] =
                m_gatewayStatusFactory->createGatewayInfo(_status->uuid());
        }
        gatewayStatus = m_gatewayInfos.at(_status->uuid());
    }
    gatewayStatus->update(_p2pNodeID, _status);
}

void PeersRouterTable::removeNodeFromGatewayInfo(P2pID const& _p2pID)
{
    ReadGuard l(x_gatewayInfos);
    for (auto const& it : m_gatewayInfos)
    {
        it.second->removeP2PNode(_p2pID);
    }
}

// broadcast message to given group
void PeersRouterTable::asyncBroadcastMsg(
    uint16_t _type, std::string const& _groupID, P2PMessage::Ptr _msg)
{
    std::vector<std::string> selectedPeers;
    {
        ReadGuard l(x_gatewayInfos);
        for (auto const& it : m_gatewayInfos)
        {
            // not broadcast message to the gateway-self
            if (it.first == m_uuid)
            {
                continue;
            }
            std::string p2pNodeID;
            if (it.second->randomChooseP2PNode(p2pNodeID, _type, _groupID))
            {
                selectedPeers.emplace_back(p2pNodeID);
            }
        }
    }
    ROUTER_LOG(TRACE) << LOG_BADGE("PeersRouterTable")
                      << LOG_DESC("asyncBroadcastMsg: randomChooseP2PNode") << LOG_KV("type", _type)
                      << LOG_KV("payloadSize", _msg->payload()->size())
                      << LOG_KV("peersSize", selectedPeers.size());
    for (auto const& peer : selectedPeers)
    {
        ROUTER_LOG(TRACE) << LOG_BADGE("PeersRouterTable") << LOG_DESC("asyncBroadcastMsg")
                          << LOG_KV("type", _type) << LOG_KV("dst", peer);
        m_p2pInterface->asyncSendMessageByNodeID(peer, _msg, CallbackFuncWithSession());
    }
}