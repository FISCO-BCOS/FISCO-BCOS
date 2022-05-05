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
 * @file ProGatewayNodeManager.h
 * @author: yujiechen
 * @date 2021-10-28
 */
#pragma once
#include "GatewayNodeManager.h"
#include <bcos-utilities/Timer.h>
namespace bcos
{
namespace gateway
{
class ProGatewayNodeManager : public GatewayNodeManager
{
public:
    using Ptr = std::shared_ptr<ProGatewayNodeManager>;
    ProGatewayNodeManager(std::string const& _uuid, P2pID const& _nodeID,
        std::shared_ptr<bcos::crypto::KeyFactory> _keyFactory, P2PInterface::Ptr _p2pInterface)
      : GatewayNodeManager(_uuid, _nodeID, _keyFactory, _p2pInterface)
    {
        m_nodeAliveDetector =
            std::make_shared<Timer>(c_tarsAdminRefreshTimeInterval, "nodeUpdater");
        m_nodeAliveDetector->registerTimeoutHandler([this]() { DetectNodeAlive(); });
    }

    void start() override
    {
        GatewayNodeManager::start();
        m_nodeAliveDetector->start();
    }
    void stop() override
    {
        GatewayNodeManager::stop();
        m_nodeAliveDetector->stop();
    }
    void updateFrontServiceInfo(bcos::group::GroupInfo::Ptr _groupInfo) override;

private:
    virtual void DetectNodeAlive();

private:
    std::shared_ptr<Timer> m_nodeAliveDetector;
    // Note: since tars need at-least 1min to update the endpoint info, we schedule DetectNodeAlive
    // every 1min
    uint64_t c_tarsAdminRefreshTimeInterval = 60 * 1000;
};
}  // namespace gateway
}  // namespace bcos