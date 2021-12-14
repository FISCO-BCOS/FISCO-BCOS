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
 * @brief GroupManager.cpp
 * @file GroupManager.cpp
 * @author: yujiechen
 * @date 2021-10-11
 */
#include "GroupManager.h"
#include <bcos-framework/interfaces/protocol/ServiceDesc.h>
#include <cstdint>
using namespace bcos;
using namespace bcos::group;
using namespace bcos::rpc;
using namespace bcos::protocol;

void GroupManager::updateGroupInfo(bcos::group::GroupInfo::Ptr _groupInfo)
{
    WriteGuard l(x_nodeServiceList);
    auto const& groupID = _groupInfo->groupID();
    if (!m_groupInfos.count(groupID))
    {
        m_groupInfos[groupID] = _groupInfo;
        GROUP_LOG(INFO) << LOG_DESC("updateGroupInfo") << printGroupInfo(_groupInfo);
        m_groupInfoNotifier(_groupInfo);
        return;
    }
    auto nodeInfos = _groupInfo->nodeInfos();
    for (auto const& it : nodeInfos)
    {
        updateNodeServiceWithoutLock(groupID, it.second);
    }
}

void GroupManager::updateNodeServiceWithoutLock(
    std::string const& _groupID, ChainNodeInfo::Ptr _nodeInfo)
{
    auto nodeAppName = _nodeInfo->nodeName();
    // the node has already been existed
    if (m_nodeServiceList.count(_groupID) && m_nodeServiceList[_groupID].count(nodeAppName))
    {
        return;
    }
    // a started node
    auto nodeService = m_nodeServiceFactory->buildNodeService(m_chainID, _groupID, _nodeInfo);
    if (!nodeService)
    {
        return;
    }
    m_nodeServiceList[_groupID][nodeAppName] = nodeService;
    auto groupInfo = m_groupInfos[_groupID];
    groupInfo->appendNodeInfo(_nodeInfo);
    m_groupInfoNotifier(groupInfo);
    BCOS_LOG(INFO) << LOG_DESC("buildNodeService for the started new node")
                   << printNodeInfo(_nodeInfo) << printGroupInfo(groupInfo);
}

bcos::protocol::BlockNumber GroupManager::getBlockNumberByGroup(const std::string& _groupID)
{
    ReadGuard l(x_groupBlockInfos);
    if (!m_groupBlockInfos.count(_groupID))
    {
        return -1;
    }

    return m_groupBlockInfos.at(_groupID);
}

NodeService::Ptr GroupManager::selectNode(std::string const& _groupID) const
{
    auto nodeName = selectNodeByBlockNumber(_groupID);
    if (nodeName.size() == 0)
    {
        return selectNodeRandomly(_groupID);
    }
    return queryNodeService(_groupID, nodeName);
}
std::string GroupManager::selectNodeByBlockNumber(std::string const& _groupID) const
{
    ReadGuard l(x_groupBlockInfos);
    if (!m_nodesWithLatestBlockNumber.count(_groupID) ||
        m_nodesWithLatestBlockNumber.at(_groupID).size() == 0)
    {
        return "";
    }
    srand(utcTime());
    auto const& nodesList = m_nodesWithLatestBlockNumber.at(_groupID);
    auto selectNodeIndex = rand() % nodesList.size();
    auto it = nodesList.begin();
    if (selectNodeIndex > 0)
    {
        std::advance(it, selectNodeIndex);
    }
    return *it;
}

NodeService::Ptr GroupManager::selectNodeRandomly(std::string const& _groupID) const
{
    ReadGuard l(x_nodeServiceList);
    if (!m_groupInfos.count(_groupID))
    {
        return nullptr;
    }
    if (!m_nodeServiceList.count(_groupID))
    {
        return nullptr;
    }
    auto const& groupInfo = m_groupInfos.at(_groupID);
    auto const& nodeInfos = groupInfo->nodeInfos();
    for (auto const& it : nodeInfos)
    {
        auto const& node = it.second;
        if (m_nodeServiceList.at(_groupID).count(node->nodeName()))
        {
            auto nodeService = m_nodeServiceList.at(_groupID).at(node->nodeName());
            if (nodeService)
            {
                return nodeService;
            }
        }
    }
    return nullptr;
}

NodeService::Ptr GroupManager::queryNodeService(
    std::string const& _groupID, std::string const& _nodeName) const
{
    ReadGuard l(x_nodeServiceList);
    if (m_nodeServiceList.count(_groupID) && m_nodeServiceList.at(_groupID).count(_nodeName))
    {
        return m_nodeServiceList.at(_groupID).at(_nodeName);
    }
    return nullptr;
}

NodeService::Ptr GroupManager::getNodeService(
    std::string const& _groupID, std::string const& _nodeName) const
{
    if (_nodeName.size() > 0)
    {
        return queryNodeService(_groupID, _nodeName);
    }
    return selectNode(_groupID);
}

void GroupManager::updateGroupStatus()
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

std::map<std::string, std::set<std::string>> GroupManager::checkNodeStatus()
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

void GroupManager::removeUnreachableNodeService(
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
void GroupManager::removeGroupBlockInfo(
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