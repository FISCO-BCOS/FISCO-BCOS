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
 * @file DynamicGatewayNodeManager.cpp
 * @author: yujiechen
 * @date 2021-10-28
 */
#include "DynamicGatewayNodeManager.h"

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::protocol;

void DynamicGatewayNodeManager::updateFrontServiceInfo()
{
    m_frontServiceInfoUpdater->restart();
    if (utcTime() - m_startT < c_tarsAdminRefreshInitTime)
    {
        return;
    }
    bool updated = false;
    UpgradableGuard l(x_frontServiceInfos);
    for (auto pnodesInfo = m_frontServiceInfos.begin(); pnodesInfo != m_frontServiceInfos.end();)
    {
        auto& nodesInfo = pnodesInfo->second;
        for (auto pFrontService = nodesInfo.begin(); pFrontService != nodesInfo.end();)
        {
            auto frontService = pFrontService->second;
            if (frontService->unreachable())
            {
                NODE_MANAGER_LOG(INFO) << LOG_DESC("remove FrontService for disconnect")
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
            pnodesInfo = m_frontServiceInfos.erase(pnodesInfo);
            updated = true;
            continue;
        }
        pnodesInfo++;
    }
    if (!updated)
    {
        return;
    }
    increaseSeq();
    notifyNodeIDs2FrontService();
}

void DynamicGatewayNodeManager::updateFrontServiceInfo(bcos::group::GroupInfo::Ptr _groupInfo)
{
    UpgradableGuard l(x_frontServiceInfos);
    auto const& groupID = _groupInfo->groupID();
    auto const& nodeInfos = _groupInfo->nodeInfos();
    bool frontServiceUpdated = false;
    for (auto const& it : nodeInfos)
    {
        // the node is not-started
        auto const& nodeInfo = it.second;
        auto const& nodeID = nodeInfo->nodeID();
        // the node is started
        if (m_frontServiceInfos.count(groupID) && m_frontServiceInfos[groupID].count(nodeID))
        {
            continue;
        }
        // createFrontService
        auto serviceName = nodeInfo->serviceName(bcos::protocol::FRONT);
        if (serviceName.size() == 0)
        {
            continue;
        }
        auto frontService =
            createServiceClient<bcostars::FrontServiceClient, bcostars::FrontServicePrx>(
                serviceName, FRONT_SERVANT_NAME, m_keyFactory);
        UpgradeGuard ul(l);
        m_frontServiceInfos[groupID][nodeID] =
            std::make_shared<FrontServiceInfo>(frontService.first, frontService.second);
        NODE_MANAGER_LOG(INFO)
            << LOG_DESC("updateFrontServiceInfo: insert frontService for the started node")
            << LOG_KV("nodeId", nodeInfo->nodeID()) << LOG_KV("serviceName", serviceName)
            << printNodeInfo(nodeInfo);
        frontServiceUpdated = true;
        increaseSeq();
    }
    if (!frontServiceUpdated)
    {
        return;
    }
    notifyNodeIDs2FrontService();
}