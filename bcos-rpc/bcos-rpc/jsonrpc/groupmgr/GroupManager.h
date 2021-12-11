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
 * @brief GroupManager.h
 * @file GroupManager.h
 * @author: yujiechen
 * @date 2021-10-11
 */
#pragma once
#include "NodeService.h"
#include <bcos-framework/libutilities/Timer.h>
namespace bcos
{
namespace rpc
{
class GroupManager
{
public:
    using Ptr = std::shared_ptr<GroupManager>;
    GroupManager(std::string const& _chainID, NodeServiceFactory::Ptr _nodeServiceFactory)
      : m_chainID(_chainID), m_nodeServiceFactory(_nodeServiceFactory)
    {
        m_startTime = utcTime();
        m_groupStatusUpdater = std::make_shared<Timer>(1000);
        m_groupStatusUpdater->start();
        m_groupStatusUpdater->registerTimeoutHandler(
            boost::bind(&GroupManager::updateGroupStatus, this));
    }
    virtual ~GroupManager() {}

    virtual void updateGroupInfo(bcos::group::GroupInfo::Ptr _groupInfo);

    virtual NodeService::Ptr getNodeService(
        std::string const& _groupID, std::string const& _nodeName) const;

    std::string const& chainID() const { return m_chainID; }

    virtual bcos::group::GroupInfo::Ptr getGroupInfo(std::string const& _groupID)
    {
        ReadGuard l(x_nodeServiceList);
        if (m_groupInfos.count(_groupID))
        {
            return m_groupInfos[_groupID];
        }
        return nullptr;
    }

    virtual bcos::group::ChainNodeInfo::Ptr getNodeInfo(
        std::string const& _groupID, std::string const& _nodeName)
    {
        ReadGuard l(x_nodeServiceList);
        if (!m_groupInfos.count(_groupID))
        {
            return nullptr;
        }
        auto groupInfo = m_groupInfos[_groupID];
        return groupInfo->nodeInfo(_nodeName);
    }

    virtual std::set<std::string> groupList()
    {
        std::set<std::string> groupList;
        ReadGuard l(x_nodeServiceList);
        for (auto const& it : m_groupInfos)
        {
            if (it.second->nodesNum() == 0)
            {
                continue;
            }
            groupList.insert(it.first);
        }
        return groupList;
    }

    virtual std::vector<bcos::group::GroupInfo::Ptr> groupInfoList()
    {
        std::vector<bcos::group::GroupInfo::Ptr> groupList;
        ReadGuard l(x_nodeServiceList);
        for (auto const& it : m_groupInfos)
        {
            groupList.push_back(it.second);
        }
        return groupList;
    }

    virtual void updateGroupBlockInfo(std::string const& _groupID, std::string const& _nodeName,
        bcos::protocol::BlockNumber _blockNumber)
    {
        UpgradableGuard l(x_groupBlockInfos);
        if (m_groupBlockInfos.count(_groupID))
        {
            // expired block
            if (m_groupBlockInfos[_groupID] > _blockNumber)
            {
                return;
            }
            // has already in the m_nodesWithLatestBlockNumber
            if (m_groupBlockInfos[_groupID] == _blockNumber &&
                m_nodesWithLatestBlockNumber.count(_groupID) &&
                m_nodesWithLatestBlockNumber[_groupID].count(_nodeName))
            {
                return;
            }
        }
        UpgradeGuard ul(l);
        bcos::protocol::BlockNumber oldBlockNumber = 0;
        if (m_groupBlockInfos.count(_groupID))
        {
            oldBlockNumber = m_groupBlockInfos[_groupID];
        }
        if (!m_nodesWithLatestBlockNumber.count(_groupID))
        {
            m_nodesWithLatestBlockNumber[_groupID] = std::set<std::string>();
        }
        // nodes with newer highest block
        if (oldBlockNumber < _blockNumber)
        {
            m_groupBlockInfos[_groupID] = _blockNumber;
            m_nodesWithLatestBlockNumber[_groupID].clear();
        }
        // nodes with the same highest block
        (m_nodesWithLatestBlockNumber[_groupID]).insert(_nodeName);
        BCOS_LOG(DEBUG) << LOG_DESC("updateGroupBlockInfo for receive block notify")
                        << LOG_KV("group", _groupID) << LOG_KV("node", _nodeName)
                        << LOG_KV("block", _blockNumber);
    }

    virtual void registerGroupInfoNotifier(
        std::function<void(bcos::group::GroupInfo::Ptr)> _callback)
    {
        m_groupInfoNotifier = _callback;
    }

    virtual bcos::protocol::BlockNumber getBlockNumberByGroup(const std::string& _groupID);

protected:
    GroupManager(std::string const& _chainID) : m_chainID(_chainID) {}
    virtual void updateGroupStatus();

    void updateNodeServiceWithoutLock(
        std::string const& _groupID, bcos::group::ChainNodeInfo::Ptr _nodeInfo);


    virtual NodeService::Ptr selectNode(std::string const& _groupID) const;
    virtual std::string selectNodeByBlockNumber(std::string const& _groupID) const;
    virtual NodeService::Ptr selectNodeRandomly(std::string const& _groupID) const;
    virtual NodeService::Ptr queryNodeService(
        std::string const& _groupID, std::string const& _nodeName) const;
    virtual void removeGroupBlockInfo(
        std::map<std::string, std::set<std::string>> const& _unreachableNodes);
    virtual void removeUnreachableNodeService(
        std::map<std::string, std::set<std::string>> const& _unreachableNodes);
    virtual std::map<std::string, std::set<std::string>> checkNodeStatus();

protected:
    std::string m_chainID;
    NodeServiceFactory::Ptr m_nodeServiceFactory;

    // map between groupID to groupInfo
    std::map<std::string, bcos::group::GroupInfo::Ptr> m_groupInfos;

    // map between nodeName to NodeService
    std::map<std::string, std::map<std::string, NodeService::Ptr>> m_nodeServiceList;
    mutable SharedMutex x_nodeServiceList;

    std::map<std::string, std::set<std::string>> m_nodesWithLatestBlockNumber;
    std::map<std::string, bcos::protocol::BlockNumber> m_groupBlockInfos;
    mutable SharedMutex x_groupBlockInfos;

    std::shared_ptr<Timer> m_groupStatusUpdater;
    std::function<void(bcos::group::GroupInfo::Ptr)> m_groupInfoNotifier;

    uint64_t c_tarsAdminRefreshInitTime = 120 * 1000;
    uint64_t m_startTime = 0;
};
}  // namespace rpc
}  // namespace bcos