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
 * @brief LocalGroupManager.h
 * @file LocalGroupManager.h
 * @author: yujiechen
 * @date 2021-10-11
 */
#pragma once
#include <bcos-rpc/jsonrpc/groupmgr/GroupManager.h>
#include <bcos-rpc/jsonrpc/groupmgr/NodeService.h>
namespace bcos
{
namespace rpc
{
class LocalGroupManager : public GroupManager
{
public:
    using Ptr = std::shared_ptr<LocalGroupManager>;
    LocalGroupManager(std::string const& _chainID, bcos::group::GroupInfo::Ptr _groupInfo,
        NodeService::Ptr _nodeService)
      : GroupManager(_chainID), m_nodeService(_nodeService), m_groupInfo(_groupInfo)
    {}

    ~LocalGroupManager() override {}

    NodeService::Ptr getNodeService(std::string const& _groupID, std::string const&) const override
    {
        if (_groupID.size() > 0 && _groupID != m_groupInfo->groupID())
        {
            return nullptr;
        }
        return m_nodeService;
    }

    std::set<std::string> groupList() override
    {
        return std::set<std::string>{m_groupInfo->groupID()};
    }

    bcos::group::GroupInfo::Ptr getGroupInfo(std::string const& _groupID) override
    {
        if (m_groupInfo->groupID() == _groupID)
        {
            return m_groupInfo;
        }
        return nullptr;
    }

    bcos::group::ChainNodeInfo::Ptr getNodeInfo(
        std::string const& _groupID, std::string const& _nodeName) override
    {
        if (m_groupInfo->groupID() != _groupID)
        {
            return nullptr;
        }
        return m_groupInfo->nodeInfo(_nodeName);
    }

    void updateGroupBlockInfo(
        std::string const&, std::string const&, bcos::protocol::BlockNumber) override
    {}

    std::vector<bcos::group::GroupInfo::Ptr> groupInfoList() override
    {
        std::vector<bcos::group::GroupInfo::Ptr> groupList;
        groupList.emplace_back(m_groupInfo);
        return groupList;
    }

private:
    NodeService::Ptr m_nodeService;
    bcos::group::GroupInfo::Ptr m_groupInfo;
};
}  // namespace rpc
}  // namespace bcos