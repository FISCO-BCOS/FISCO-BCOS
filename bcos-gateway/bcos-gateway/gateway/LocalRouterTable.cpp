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
 * @file LocalRouterTable.cpp
 * @author: octopus
 * @date 2021-12-29
 */
#include "LocalRouterTable.h"
#include <bcos-framework/interfaces/protocol/ServiceDesc.h>
#include <bcos-gateway/Common.h>
using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::gateway;
using namespace bcos::front;
using namespace bcos::crypto;

FrontServiceInfo::Ptr LocalRouterTable::getFrontService(
    const std::string& _groupID, NodeIDPtr _nodeID)
{
    ReadGuard l(x_nodeList);
    if (!m_nodeList.count(_groupID) || !m_nodeList[_groupID].count(_nodeID->hex()))
    {
        return nullptr;
    }
    return m_nodeList[_groupID][_nodeID->hex()];
}

std::vector<FrontServiceInfo::Ptr> LocalRouterTable::getGroupFrontServiceList(
    const std::string& _groupID)
{
    std::vector<FrontServiceInfo::Ptr> nodeServiceList;
    ReadGuard l(x_nodeList);
    if (!m_nodeList.count(_groupID))
    {
        return nodeServiceList;
    }
    for (auto const& it : m_nodeList.at(_groupID))
    {
        nodeServiceList.emplace_back(it.second);
    }
    return nodeServiceList;
}

void LocalRouterTable::getGroupNodeIDList(const std::string& _groupID, NodeIDs& _nodeIDList)
{
    ReadGuard l(x_nodeList);
    if (!m_nodeList.count(_groupID))
    {
        return;
    }
    for (auto const& item : m_nodeList[_groupID])
    {
        auto bytes = bcos::fromHexString(item.first);
        _nodeIDList.emplace_back(m_keyFactory->createKey(*bytes.get()));
    }
}

std::map<std::string, std::set<std::string>> LocalRouterTable::nodeListInfo()
{
    std::map<std::string, std::set<std::string>> nodeList;
    ReadGuard l(x_nodeList);
    for (auto const& it : m_nodeList)
    {
        auto groupID = it.first;
        if (!nodeList.count(groupID))
        {
            nodeList[groupID] = std::set<std::string>();
        }
        auto const& infos = it.second;
        for (auto const& nodeIt : infos)
        {
            nodeList[groupID].insert(nodeIt.first);
        }
    }
    return nodeList;
}

/**
 * @brief: insert node
 * @param _groupID: groupID
 * @param _nodeID: nodeID
 * @param _frontService: FrontService
 */
bool LocalRouterTable::insertNode(
    const std::string& _groupID, NodeIDPtr _nodeID, FrontServiceInterface::Ptr _frontService)
{
    auto nodeIDStr = _nodeID->hex();
    UpgradableGuard l(x_nodeList);
    if (m_nodeList.count(_groupID) && m_nodeList[_groupID].count(nodeIDStr))
    {
        ROUTER_LOG(INFO) << LOG_DESC("insertNode: the node has already existed")
                         << LOG_KV("groupID", _groupID) << LOG_KV("nodeID", nodeIDStr);
        return false;
    }
    auto frontServiceInfo = std::make_shared<FrontServiceInfo>(nodeIDStr, _frontService, nullptr);
    UpgradeGuard ul(l);
    m_nodeList[_groupID][nodeIDStr] = frontServiceInfo;
    ROUTER_LOG(INFO) << LOG_DESC("insertNode") << LOG_KV("groupID", _groupID)
                     << LOG_KV("nodeID", nodeIDStr);
    return true;
}

/**
 * @brief: removeNode
 * @param _groupID: groupID
 * @param _nodeID: nodeID
 */
bool LocalRouterTable::removeNode(const std::string& _groupID, NodeIDPtr _nodeID)
{
    auto nodeIDStr = _nodeID->hex();
    UpgradableGuard l(x_nodeList);
    if (!m_nodeList.count(_groupID) || !m_nodeList[_groupID].count(nodeIDStr))
    {
        ROUTER_LOG(INFO) << LOG_DESC("removeNode: the node is not registered")
                         << LOG_KV("groupID", _groupID) << LOG_KV("nodeID", nodeIDStr);
        return false;
    }
    // erase the node from m_nodeList
    UpgradeGuard ul(l);
    (m_nodeList[_groupID]).erase(nodeIDStr);
    if (m_nodeList.at(_groupID).empty())
    {
        m_nodeList.erase(_groupID);
    }
    ROUTER_LOG(INFO) << LOG_DESC("removeNode") << LOG_KV("groupID", _groupID)
                     << LOG_KV("nodeID", nodeIDStr);
    return true;
}

bool LocalRouterTable::updateGroupNodeInfos(bcos::group::GroupInfo::Ptr _groupInfo)
{
    UpgradableGuard l(x_nodeList);
    auto const& groupID = _groupInfo->groupID();
    auto const& nodeInfos = _groupInfo->nodeInfos();
    bool frontServiceUpdated = false;
    for (auto const& it : nodeInfos)
    {
        auto const& nodeInfo = it.second;
        auto const& nodeID = nodeInfo->nodeID();
        // the node is registered
        if (m_nodeList.count(groupID) && m_nodeList[groupID].count(nodeID))
        {
            continue;
        }
        // insert the new node
        auto serviceName = nodeInfo->serviceName(bcos::protocol::FRONT);
        if (serviceName.size() == 0)
        {
            continue;
        }
        auto frontService =
            createServiceClient<bcostars::FrontServiceClient, bcostars::FrontServicePrx>(
                serviceName, FRONT_SERVANT_NAME, m_keyFactory);
        UpgradeGuard ul(l);
        auto frontServiceInfo = std::make_shared<FrontServiceInfo>(
            nodeInfo->nodeID(), frontService.first, frontService.second);
        m_nodeList[groupID][nodeID] = frontServiceInfo;
        ROUTER_LOG(INFO) << LOG_DESC("updateGroupNodeInfos: insert frontService for the node")
                         << LOG_KV("nodeID", nodeInfo->nodeID())
                         << LOG_KV("serviceName", serviceName) << printNodeInfo(nodeInfo);
        frontServiceUpdated = true;
    }
    return frontServiceUpdated;
}

bool LocalRouterTable::eraseUnreachableNodes()
{
    bool updated = false;
    UpgradableGuard l(x_nodeList);
    for (auto it = m_nodeList.begin(); it != m_nodeList.end();)
    {
        auto& nodesInfo = it->second;
        for (auto pFrontService = nodesInfo.begin(); pFrontService != nodesInfo.end();)
        {
            auto frontService = pFrontService->second;
            if (frontService->unreachable())
            {
                ROUTER_LOG(INFO) << LOG_DESC("remove FrontService for unreachable")
                                 << LOG_KV("node", pFrontService->first);
                UpgradeGuard ul(l);
                pFrontService = nodesInfo.erase(pFrontService);
                updated = true;
                continue;
            }
            pFrontService++;
        }
        if (nodesInfo.size() == 0)
        {
            UpgradeGuard ul(l);
            it = m_nodeList.erase(it);
            updated = true;
            continue;
        }
        it++;
    }
    return updated;
}