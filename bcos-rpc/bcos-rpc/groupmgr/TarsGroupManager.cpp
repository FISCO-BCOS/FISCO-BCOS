/**
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
 * @brief TarsGroupManager.cpp
 * @file TarsGroupManager.cpp
 * @author: yujiechen
 * @date 2021-10-11
 */
#include "TarsGroupManager.h"
#include <bcos-framework/interfaces/protocol/ServiceDesc.h>
#include <cstdint>
using namespace bcos;
using namespace bcos::group;
using namespace bcos::rpc;
using namespace bcos::protocol;

void TarsGroupManager::updateGroupStatus()
{
    m_groupStatusUpdater->restart();
    if (utcTime() - m_startTime <= c_tarsAdminRefreshInitTime)
    {
        return;
    }
    auto unreachableNodes = checkNodeStatus();
    if (unreachableNodes.size() == 0)
    {
        return;
    }
    removeUnreachableNodeService(unreachableNodes);
    removeGroupBlockInfo(unreachableNodes);
}

std::map<std::string, std::set<std::string>> TarsGroupManager::checkNodeStatus()
{
    ReadGuard l(x_nodeServiceList);
    std::map<std::string, std::set<std::string>> unreachableNodes;
    for (auto const& it : m_groupInfos)
    {
        bool groupInfoUpdated = false;
        auto groupInfo = it.second;
        auto groupID = groupInfo->groupID();
        auto const& groupNodeList = groupInfo->nodeInfos();
        for (auto const& nodeInfo : groupNodeList)
        {
            if (!m_nodeServiceList.count(groupID) ||
                !m_nodeServiceList[groupID].count(nodeInfo.first) ||
                m_nodeServiceList[groupID][nodeInfo.first]->unreachable())
            {
                groupInfo->removeNodeInfo(nodeInfo.second);
                unreachableNodes[groupID].insert(nodeInfo.first);
                groupInfoUpdated = true;
                continue;
            }
        }
        // notify the updated groupInfo to the sdk
        if (m_groupInfoNotifier && groupInfoUpdated)
        {
            m_groupInfoNotifier(groupInfo);
        }
    }
    return unreachableNodes;
}

void TarsGroupManager::removeUnreachableNodeService(
    std::map<std::string, std::set<std::string>> const& _unreachableNodes)
{
    WriteGuard l(x_nodeServiceList);
    for (auto const& it : _unreachableNodes)
    {
        auto group = it.first;
        if (!m_nodeServiceList.count(group))
        {
            continue;
        }
        auto const& nodeList = it.second;
        for (auto const& node : nodeList)
        {
            BCOS_LOG(INFO) << LOG_DESC("GroupManager: removeUnreachablNodeService")
                           << LOG_KV("group", group) << LOG_KV("node", node);
            m_nodeServiceList[group].erase(node);
        }
        if (m_nodeServiceList[group].size() == 0)
        {
            m_nodeServiceList.erase(group);
        }
    }
}
void TarsGroupManager::removeGroupBlockInfo(
    std::map<std::string, std::set<std::string>> const& _unreachableNodes)
{
    WriteGuard l(x_groupBlockInfos);
    for (auto const& it : _unreachableNodes)
    {
        auto group = it.first;
        if (!m_nodesWithLatestBlockNumber.count(group))
        {
            m_groupBlockInfos.erase(group);
            continue;
        }
        if (!m_groupBlockInfos.count(group))
        {
            m_nodesWithLatestBlockNumber.erase(group);
            continue;
        }
        auto const& nodeList = it.second;
        for (auto const& node : nodeList)
        {
            m_nodesWithLatestBlockNumber[group].erase(node);
        }
        if (m_nodesWithLatestBlockNumber[group].size() == 0)
        {
            m_groupBlockInfos.erase(group);
            m_nodesWithLatestBlockNumber.erase(group);
        }
    }
}