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
 * @brief AirGroupManager.h
 * @file AirGroupManager.h
 * @author: yujiechen
 * @date 2021-10-11
 */
#pragma once
#include <bcos-rpc/groupmgr/GroupManager.h>
#include <bcos-rpc/groupmgr/NodeService.h>
namespace bcos
{
namespace rpc
{
class AirGroupManager : public GroupManager
{
public:
    using Ptr = std::shared_ptr<AirGroupManager>;
    AirGroupManager(std::string const& _chainID, bcos::group::GroupInfo::Ptr _groupInfo,
        NodeService::Ptr _nodeService)
      : GroupManager(_chainID), m_nodeService(_nodeService), m_groupInfo(_groupInfo)
    {}

    ~AirGroupManager() override {}
    virtual void init() { initNodeInfo(m_groupInfo->groupID(), "localNode", m_nodeService); }

    NodeService::Ptr getNodeService(std::string const& _groupID, std::string const&) const override
    {
        ReadGuard l(x_groupInfo);
        if (_groupID.size() > 0 && _groupID != m_groupInfo->groupID())
        {
            return nullptr;
        }
        return m_nodeService;
    }

    std::set<std::string> groupList() override
    {
        ReadGuard l(x_groupInfo);
        return std::set<std::string>{m_groupInfo->groupID()};
    }

    bcos::group::GroupInfo::Ptr getGroupInfo(std::string const& _groupID) override
    {
        ReadGuard l(x_groupInfo);
        if (m_groupInfo->groupID() == _groupID)
        {
            return m_groupInfo;
        }
        return nullptr;
    }

    bcos::group::ChainNodeInfo::Ptr getNodeInfo(
        std::string const& _groupID, std::string const& _nodeName) override
    {
        ReadGuard l(x_groupInfo);
        if (m_groupInfo->groupID() != _groupID)
        {
            return nullptr;
        }
        return m_groupInfo->nodeInfo(_nodeName);
    }

    std::vector<bcos::group::GroupInfo::Ptr> groupInfoList() override
    {
        std::vector<bcos::group::GroupInfo::Ptr> groupList;
        groupList.emplace_back(m_groupInfo);
        return groupList;
    }

    bool updateGroupInfo(bcos::group::GroupInfo::Ptr _groupInfo) override
    {
        {
            WriteGuard l(x_groupInfo);
            m_groupInfo = _groupInfo;
        }
        ReadGuard l(x_groupInfo);
        if (m_groupInfoNotifier)
        {
            m_groupInfoNotifier(_groupInfo);
        }
        return true;
    }

private:
    NodeService::Ptr m_nodeService;
    bcos::group::GroupInfo::Ptr m_groupInfo;
    mutable SharedMutex x_groupInfo;
};
}  // namespace rpc
}  // namespace bcos