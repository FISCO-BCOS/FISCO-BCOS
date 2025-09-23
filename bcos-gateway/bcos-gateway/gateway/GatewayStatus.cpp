/*
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @file GatewayStatus.h
 * @author: yujiechen
 * @date 2022-1-07
 */
#include "GatewayStatus.h"
#include <mutex>
#include <random>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::protocol;

void GatewayStatus::update(std::string const& _p2pNodeID, GatewayNodeStatus::ConstPtr _nodeStatus)
{
    if (_nodeStatus->uuid() != m_uuid)
    {
        return;
    }
    // TODO: optimize the count below
    std::lock_guard<std::mutex> guard(x_groupP2PNodeList);
    auto const& groupNodeInfos = _nodeStatus->groupNodeInfos();
    for (auto const& node : groupNodeInfos)
    {
        auto const& groupID = node->groupID();
        auto type = (GroupType)(node->type());
        if (m_groupP2PNodeList.count(groupID) && m_groupP2PNodeList[groupID].count(type) &&
            m_groupP2PNodeList[groupID][type].count(_p2pNodeID))
        {
            continue;
        }
        // remove the _p2pNodeID from the cache
        removeP2PIDWithoutLock(groupID, _p2pNodeID);
        // insert the new p2pNodeID
        if (!m_groupP2PNodeList.count(groupID) || !m_groupP2PNodeList[groupID].count(type))
        {
            m_groupP2PNodeList[groupID][type] = std::set<std::string>();
        }
        (m_groupP2PNodeList[groupID][type]).insert(_p2pNodeID);
        ROUTER_LOG(INFO) << LOG_DESC("GatewayStatus: update") << LOG_KV("group", groupID)
                         << LOG_KV("type", type) << LOG_KV("p2pID", _p2pNodeID);
    }
}

bool GatewayStatus::randomChooseP2PNode(
    std::string& _p2pNodeID, uint16_t _type, std::string_view _groupID) const
{
    auto ret = false;
    // If need to send a message to a consensus node, select the consensus node first
    if (_type & NodeType::CONSENSUS_NODE)
    {
        ret = randomChooseNode(_p2pNodeID, GroupType::GROUP_WITH_CONSENSUS_NODE, _groupID);
    }
    // select the observer node
    if (!ret && _type & NodeType::OBSERVER_NODE)
    {
        ret = randomChooseNode(_p2pNodeID, GroupType::GROUP_WITHOUT_CONSENSUS_NODE, _groupID);
    }
    // select the OUTSIDE_GROUP(AMOP message needed)
    if (!ret && _type & NodeType::FREE_NODE)
    {
        ret = randomChooseNode(_p2pNodeID, GroupType::OUTSIDE_GROUP, _groupID);
    }
    return ret;
}

bool GatewayStatus::randomChooseNode(
    std::string& _choosedNode, GroupType _type, std::string_view _groupID) const
{
    const std::set<std::string>* p2pNodeList = nullptr;
    std::map<std::string, std::map<bcos::gateway::GroupType, std::set<std::string>>>::const_iterator
        it;
    {
        // TODO: if the lock below can be removed?
        std::lock_guard<std::mutex> guard(x_groupP2PNodeList);
        it = m_groupP2PNodeList.find(_groupID);
        if (it == m_groupP2PNodeList.end())
        {
            return false;
        }
    }
    auto it2 = it->second.find(_type);
    if (it2 == it->second.end())
    {
        return false;
    }
    p2pNodeList = &it2->second;
    if (p2pNodeList->empty())
    {
        return false;
    }

    static thread_local std::mt19937 random(std::random_device{}());
    auto selectedP2PNodeIndex = random() % p2pNodeList->size();
    auto iterator = p2pNodeList->begin();
    if (selectedP2PNodeIndex > 0)
    {
        std::advance(iterator, selectedP2PNodeIndex);
    }
    _choosedNode = *iterator;
    return true;
}

void GatewayStatus::removeP2PIDWithoutLock(
    std::string const& _groupID, std::string const& _p2pNodeID)
{
    auto it = m_groupP2PNodeList.find(_groupID);
    if (it == m_groupP2PNodeList.end())
    {
        return;
    }
    auto& p2pNodeList = it->second;
    for (auto it = p2pNodeList.begin(); it != p2pNodeList.end();)
    {
        auto p2pNodeIDIt = it->second.find(_p2pNodeID);
        if (p2pNodeIDIt != it->second.end())
        {
            it->second.erase(p2pNodeIDIt);
        }
        if (it->second.empty())
        {
            it = p2pNodeList.erase(it);
            continue;
        }
        it++;
    }
}

void GatewayStatus::removeP2PNode(std::string const& _p2pNodeID)
{
    std::lock_guard<std::mutex> guard(x_groupP2PNodeList);
    for (auto pGroupInfo = m_groupP2PNodeList.begin(); pGroupInfo != m_groupP2PNodeList.end();)
    {
        auto& p2pNodeList = m_groupP2PNodeList[pGroupInfo->first];
        removeP2PIDWithoutLock(pGroupInfo->first, _p2pNodeID);
        if (p2pNodeList.empty())
        {
            pGroupInfo = m_groupP2PNodeList.erase(pGroupInfo);
            continue;
        }
        pGroupInfo++;
    }
    ROUTER_LOG(INFO) << LOG_DESC("GatewayStatus: removeP2PNode") << LOG_KV("p2pID", _p2pNodeID);
}
