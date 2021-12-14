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
 * @file DynamicGatewayNodeManager.h
 * @author: yujiechen
 * @date 2021-10-28
 */
#pragma once
#include "GatewayNodeManager.h"
#include <bcos-framework/libutilities/Timer.h>
namespace bcos
{
namespace gateway
{
class DynamicGatewayNodeManager : public GatewayNodeManager
{
public:
    DynamicGatewayNodeManager(
        P2pID const& _nodeID, std::shared_ptr<bcos::crypto::KeyFactory> _keyFactory)
      : GatewayNodeManager(_nodeID, _keyFactory)
    {
        m_frontServiceInfoUpdater = std::make_shared<Timer>(1000, "frontServiceUpdater");
        m_frontServiceInfoUpdater->registerTimeoutHandler([this]() { updateFrontServiceInfo(); });
        m_startT = utcTime();
    }

    void start() override { m_frontServiceInfoUpdater->start(); }
    void stop() override { m_frontServiceInfoUpdater->stop(); }
    void updateFrontServiceInfo(bcos::group::GroupInfo::Ptr _groupInfo) override;

private:
    virtual void updateFrontServiceInfo();

private:
    std::shared_ptr<Timer> m_frontServiceInfoUpdater;
    uint64_t m_startT;
    uint64_t c_tarsAdminRefreshInitTime = 120 * 1000;
};
}  // namespace gateway
}  // namespace bcos