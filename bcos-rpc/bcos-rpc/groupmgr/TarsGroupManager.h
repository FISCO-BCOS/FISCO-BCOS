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
 * @brief TarsGroupManager
 * @file TarsGroupManager.h
 * @author: yujiechen
 * @date 2021-10-11
 */
#pragma once
#include "GroupManager.h"
#include "NodeService.h"
#include <bcos-utilities/Timer.h>
namespace bcos
{
namespace rpc
{
class TarsGroupManager : public GroupManager
{
public:
    using Ptr = std::shared_ptr<TarsGroupManager>;
    TarsGroupManager(std::string const& _chainID, NodeServiceFactory::Ptr _nodeServiceFactory)
      : GroupManager(_chainID, _nodeServiceFactory)
    {
        m_startTime = utcTime();
        m_groupStatusUpdater = std::make_shared<Timer>(1000);
        m_groupStatusUpdater->start();
        m_groupStatusUpdater->registerTimeoutHandler(
            boost::bind(&TarsGroupManager::updateGroupStatus, this));
    }
    virtual ~TarsGroupManager()
    {
        if (m_groupStatusUpdater)
        {
            m_groupStatusUpdater->stop();
        }
    }

protected:
    virtual void updateGroupStatus();
    virtual std::map<std::string, std::set<std::string>> checkNodeStatus();

protected:
    std::shared_ptr<Timer> m_groupStatusUpdater;
    uint64_t c_tarsAdminRefreshInitTime = 120 * 1000;
    uint64_t m_startTime = 0;
};
}  // namespace rpc
}  // namespace bcos