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
 * @file ProGatewayNodeManager.cpp
 * @author: yujiechen
 * @date 2021-10-28
 */
#include "ProGatewayNodeManager.h"

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::protocol;

ProGatewayNodeManager::ProGatewayNodeManager(std::string const& _uuid, P2pID const& _nodeID,
    std::shared_ptr<bcos::crypto::KeyFactory> _keyFactory, P2PInterface::Ptr _p2pInterface)
  : GatewayNodeManager(_uuid, _nodeID, _keyFactory, _p2pInterface)
{
    m_nodeAliveDetector =
        std::make_shared<Timer>(c_tarsAdminRefreshTimeInterval, "nodeUpdater");
    m_nodeAliveDetector->registerTimeoutHandler([this]() { detectNodeAlive(); });
}

void ProGatewayNodeManager::start()
{
    GatewayNodeManager::start();
    m_nodeAliveDetector->start();
}

void ProGatewayNodeManager::stop()
{
    GatewayNodeManager::stop();
    m_nodeAliveDetector->stop();
}

void ProGatewayNodeManager::detectNodeAlive()
{
    m_nodeAliveDetector->restart();
    auto updated = m_localRouterTable->eraseUnreachableNodes();
    if (updated)
    {
        increaseSeq();
    }

    syncLatestNodeIDList();
}

bool ProGatewayNodeManager::updateFrontServiceInfo(bcos::group::GroupInfo::Ptr _groupInfo)
{
    auto ret = GatewayNodeManager::updateFrontServiceInfo(_groupInfo);
    if (ret)
    {
        m_nodeAliveDetector->restart();
    }
    return ret;
}