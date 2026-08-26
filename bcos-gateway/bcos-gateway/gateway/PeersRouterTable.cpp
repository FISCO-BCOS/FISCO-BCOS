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
#include "bcos-utilities/BoostLog.h"
#include <boost/exception/diagnostic_information.hpp>
#include <exception>

using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::gateway;
using namespace bcos::crypto;

void PeersRouterTable::getGroupNodeInfoList(
    GroupNodeInfo::Ptr _groupInfo, const std::string& _groupID) const
{
    ReadGuard lock(x_groupNodeList);
    if (auto groupNodeIt = m_groupNodeList.find(_groupID); groupNodeIt != m_groupNodeList.end())
    {
        for (auto const& it : groupNodeIt->second)
        {
            auto nodeID = it.first;
            _groupInfo->appendNodeID(nodeID);
            if (auto nodeIt = m_nodeProtocolInfo.find(nodeID); nodeIt != m_nodeProtocolInfo.end())
            {
                _groupInfo->appendProtocol(nodeIt->second);
            }
        }
    }
}

std::set<P2pID> PeersRouterTable::queryP2pIDs(
    const std::string& _groupID, const std::string& _nodeID) const
{
    ReadGuard l(x_groupNodeList);
    auto it = m_groupNodeList.find(_groupID);
    if (it == m_groupNodeList.end())
    {
        return {};
    }
    auto it2 = it->second.find(_nodeID);
    if (it2 == it->second.end())
    {
        return {};
    }
    return it2->second;
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
            // Capture the map iterators so the reverse index can alias the stable key strings owned
            // by these nodes (try_emplace leaves an existing entry untouched, inserts otherwise).
            auto groupIt = m_groupNodeList.try_emplace(groupID).first;
            auto nodeIt = groupIt->second.try_emplace(nodeID).first;
            nodeIt->second.insert(_p2pNodeID);
            // FIB-186 (vector D): mirror the insert into the reverse index so the matching removal
            // is O(K) instead of a full-map scan. Store pointers to the key strings (not copies);
            // see the lifetime invariant on m_p2pID2GroupNodes. std::set dedups repeated pairs.
            m_p2pID2GroupNodes[_p2pNodeID].emplace(&groupIt->first, &nodeIt->first);
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
    ROUTER_LOG(INFO) << LOG_DESC("PeersRouterTable: removeP2PID")
                     << LOG_KV("p2pID", printShortP2pID(_p2pID));
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
    // FIB-186 (vector D): remove only the entries this p2pID actually holds, looked up via the
    // reverse index, instead of scanning the whole forward map. This bounds the WriteLock hold time
    // to O(K) (K = entries the peer holds), so a persistent bulk-disconnect on the teardown thread
    // cannot stall queryP2pIDs (the ReadLock on the unicast routing hot path). Pruning of emptied
    // node/group entries stays identical to the old full scan: only a set that contained _p2pID can
    // shrink, so a set the peer never joined is never visited and never pruned.
    auto revIt = m_p2pID2GroupNodes.find(_p2pID);
    if (revIt == m_p2pID2GroupNodes.end())
    {
        return;
    }
    for (auto const& [groupIDPtr, nodeIDPtr] : revIt->second)
    {
        auto groupIt = m_groupNodeList.find(*groupIDPtr);
        if (groupIt == m_groupNodeList.end())
        {
            continue;
        }
        auto nodeIt = groupIt->second.find(*nodeIDPtr);
        if (nodeIt == groupIt->second.end())
        {
            continue;
        }
        nodeIt->second.erase(_p2pID);
        if (nodeIt->second.empty())
        {
            groupIt->second.erase(nodeIt);
            if (groupIt->second.empty())
            {
                m_groupNodeList.erase(groupIt);
            }
        }
    }
    m_p2pID2GroupNodes.erase(revIt);
}

void PeersRouterTable::updatePeerNodeList(P2pID const& _p2pNodeID, GatewayNodeStatus::Ptr _status)
{
    WriteGuard l(x_peersStatus);
    m_peersStatus[_p2pNodeID] = _status;
}

void PeersRouterTable::removePeerStatus(P2pID const& _p2pNodeID)
{
    WriteGuard l(x_peersStatus);
    if (auto it = m_peersStatus.find(_p2pNodeID); it != m_peersStatus.end())
    {
        m_peersStatus.erase(it);
    }
}

PeersRouterTable::Group2NodeIDListType PeersRouterTable::peersNodeIDList(
    P2pID const& _p2pNodeID) const
{
    PeersRouterTable::Group2NodeIDListType nodeIDList;
    ReadGuard l(x_peersStatus);

    auto it = m_peersStatus.find(_p2pNodeID);
    if (it == m_peersStatus.end())
    {
        return nodeIDList;
    }
    auto const& groupNodeInfos = it->second->groupNodeInfos();
    for (auto const& it : groupNodeInfos)
    {
        auto const& groupNodeIDList = it->nodeIDList();
        auto const& nodeTypeList = it->nodeTypeList();
        for (size_t i = 0; i < groupNodeIDList.size(); ++i)
        {
            auto nodeID = groupNodeIDList[i];
            nodeIDList[it->groupID()][nodeID] = bcos::protocol::NodeType::NONE;
            if (nodeTypeList.size() > i)
            {
                auto nodeType = nodeTypeList[i];
                nodeIDList[it->groupID()][nodeID] = nodeType;
            }
        }
    }
    return nodeIDList;
}

std::set<P2pID> PeersRouterTable::getAllPeers() const
{
    std::set<P2pID> peers;
    ReadGuard l(x_peersStatus);
    for (auto const& peerInfo : m_peersStatus)
    {
        peers.insert(peerInfo.first);
    }
    return peers;
}

GatewayStatus::Ptr PeersRouterTable::gatewayInfo(std::string const& _uuid)
{
    if (auto it = m_gatewayInfos.find(_uuid); it != m_gatewayInfos.end())
    {
        return it->second;
    }
    return nullptr;
}

void PeersRouterTable::updateGatewayInfo(P2pID const& _p2pNodeID, GatewayNodeStatus::Ptr _status)
{
    GatewayStatus::Ptr gatewayStatus;
    auto [it, inserted] = m_gatewayInfos.emplace(_status->uuid(), GatewayStatus::Ptr{});
    if (inserted)
    {
        it->second = m_gatewayStatusFactory->createGatewayInfo(_status->uuid());
    }
    gatewayStatus = it->second;

    gatewayStatus->update(_p2pNodeID, _status);
}

void PeersRouterTable::removeNodeFromGatewayInfo(P2pID const& _p2pID)
{
    for (auto const& it : m_gatewayInfos)
    {
        it.second->removeP2PNode(_p2pID);
    }
}

bcos::task::Task<void> bcos::gateway::PeersRouterTable::broadcastMessage(uint16_t type,
    std::string_view group, uint16_t moduleID, const P2PMessageV2& message,
    ::ranges::any_view<bytesConstRef, ::ranges::category::forward> payloads)
{
    std::vector<std::string> selectedPeers;

    selectedPeers.reserve(m_gatewayInfos.size());
    for (auto const& it : m_gatewayInfos)
    {
        // not broadcast message to the gateway-self
        if (it.first == m_uuid || !it.second)
        {
            continue;
        }
        std::string p2pNodeID;
        if (it.second->randomChooseP2PNode(p2pNodeID, type, group))
        {
            selectedPeers.emplace_back(std::move(p2pNodeID));
        }
    }

    ROUTER_LOG(TRACE) << LOG_BADGE("PeersRouterTable")
                      << LOG_DESC("broadcastMsg: randomChooseP2PNode") << LOG_KV("nodeType", type)
                      << LOG_KV("moduleID", moduleID) << LOG_KV("peersSize", selectedPeers.size());
    for (auto const& peer : selectedPeers)
    {
        if (c_fileLogLevel <= TRACE) [[unlikely]]
        {
            ROUTER_LOG(TRACE) << LOG_BADGE("PeersRouterTable") << LOG_DESC("asyncBroadcastMsg")
                              << LOG_KV("nodeType", type) << LOG_KV("moduleID", moduleID)
                              << LOG_KV("dst", printShortP2pID(peer));
        }
        auto forkMessage = message;
        try
        {
            co_await m_p2pInterface->sendMessageByNodeID(peer, forkMessage, payloads);
        }
        catch (const std::exception& e)
        {
            ROUTER_LOG(WARNING) << "send message to nodeid: " << peer << " failed, "
                                << boost::diagnostic_information(e);
        }
    }
}
