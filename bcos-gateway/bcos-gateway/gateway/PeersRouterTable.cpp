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
using namespace bcos::gateway;
using namespace bcos::crypto;

bcos::crypto::NodeIDs PeersRouterTable::getGroupNodeIDList(const std::string& _groupID) const
{
    NodeIDs nodeIDList;
    ReadGuard l(x_groupNodeList);
    if (!m_groupNodeList.count(_groupID))
    {
        return nodeIDList;
    }
    for (auto const& it : m_groupNodeList.at(_groupID))
    {
        auto nodeID = bcos::fromHexString(it.first);
        if (!nodeID)
        {
            continue;
        }
        auto nodeIDPtr = m_keyFactory->createKey(*nodeID.get());
        nodeIDList.emplace_back(nodeIDPtr);
    }
    return nodeIDList;
}

std::set<P2pID> PeersRouterTable::queryP2pIDs(
    const std::string& _groupID, const std::string& _nodeID) const
{
    ReadGuard l(x_groupNodeList);
    if (!m_groupNodeList.count(_groupID) || !m_groupNodeList.at(_groupID).count(_nodeID))
    {
        return std::set<P2pID>;
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

void PeersRouterTable::batchInsertNodeList(
    std::string const& _p2pNodeID, std::map<std::string, std::set<std::string>> const& _nodeList)
{
    WriteGuard l(x_groupNodeList);
    for (auto const& it : _nodeList)
    {
        auto groupID = it.first;
        for (auto const& nodeID : it.second)
        {
            if (!m_groupNodeList.count(groupID) || !m_groupNodeList.at(groupID).count(nodeID))
            {
                m_groupNodeList[groupID][nodeID] = std::set<P2pID>();
            }
            m_groupNodeList[groupID][nodeID].insert(_p2pNodeID);
        }
    }
}

void PeersRouterTable::removeP2PID(const std::string& _p2pID)
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
    removePeer(_p2pID);
}

void PeersRouterTable::removePeer(std::string const& _p2pNodeID)
{
    UpgradableGuard l(x_peersNodeList);
    if (m_peersNodeList.count(_p2pNodeID))
    {
        UpgradeGuard ul(l);
        m_peersNodeList.erase(_p2pNodeID);
    }
}

void PeersRouterTable::updatePeerNodeList(
    std::string const& _p2pNodeID, std::map<std::string, std::set<std::string>> const& _nodeIDList)
{
    WriteGuard l(x_peersNodeList);
    m_peersNodeList[_p2pNodeID] = _nodeIDList;
}

std::map<std::string, std::set<std::string>> PeersRouterTable::peersNodeInfo(
    std::string const& _p2pNodeID) const
{
    ReadGuard l(x_peersNodeList);
    if (m_peersNodeList.count(_p2pNodeID))
    {
        return m_peersNodeList.at(_p2pNodeID);
    }
    return std::map<std::string, std::set<std::string>>();
}