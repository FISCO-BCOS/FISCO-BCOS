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
        m_groupStatusUpdater = std::make_shared<Timer>(c_tarsAdminRefreshTime);
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

    bool updateGroupInfo(bcos::group::GroupInfo::Ptr _groupInfo) override;

protected:
    virtual void updateGroupStatus();
    virtual std::map<std::string, std::set<std::string>> checkNodeStatus();

protected:
    std::shared_ptr<Timer> m_groupStatusUpdater;
    // Note: since tars need at-least 1min to update the endpoint info, we schedule
    // updateGroupStatus every 1min
    uint64_t c_tarsAdminRefreshTime = 60 * 1000;
};
}  // namespace rpc
}  // namespace bcos