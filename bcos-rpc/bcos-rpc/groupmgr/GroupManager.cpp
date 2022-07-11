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
 * @brief GroupManager.cpp
 * @file GroupManager.cpp
 * @author: yujiechen
 * @date 2021-10-11
 */
#include "GroupManager.h"
#include <bcos-framework/protocol/ServiceDesc.h>
#include <cstdint>
using namespace bcos;
using namespace bcos::group;
using namespace bcos::rpc;
using namespace bcos::protocol;

bool GroupManager::updateGroupInfo(bcos::group::GroupInfo::Ptr _groupInfo)
{
    if (!checkGroupInfo(_groupInfo))
    {
        return false;
    }
    bool enforceUpdate = false;
    {
        UpgradableGuard l(x_nodeServiceList);
        auto const& groupID = _groupInfo->groupID();
        if (!m_groupInfos.count(groupID))
        {
            UpgradeGuard ul(l);
            m_groupInfos[groupID] = _groupInfo;
            GROUP_LOG(INFO) << LOG_DESC("updateGroupInfo") << printGroupInfo(_groupInfo);
            m_groupInfoNotifier(_groupInfo);
            enforceUpdate = true;
        }
    }
    return updateGroupServices(_groupInfo, enforceUpdate);
}

bool GroupManager::checkGroupInfo(bcos::group::GroupInfo::Ptr _groupInfo)
{
    // check the serviceName
    auto nodeList = _groupInfo->nodeInfos();
    for (auto const& node : nodeList)
    {
        auto const& expectedRpcService = node.second->serviceName(bcos::protocol::ServiceType::RPC);
        if (expectedRpcService != m_rpcServiceName)
        {
            GROUP_LOG(INFO) << LOG_DESC("unfollowed groupInfo for inconsistent rpc service name")
                            << LOG_KV("expected", expectedRpcService)
                            << LOG_KV("selfName", m_rpcServiceName);
            return false;
        }
    }
    return true;
}
bool GroupManager::updateGroupServices(bcos::group::GroupInfo::Ptr _groupInfo, bool _enforce)
{
    auto ret = false;
    auto nodeInfos = _groupInfo->nodeInfos();
    for (auto const& it : nodeInfos)
    {
        if (updateNodeService(_groupInfo->groupID(), it.second, _enforce))
        {
            ret = true;
        }
    }
    return ret;
}

void GroupManager::removeGroupNodeList(bcos::group::GroupInfo::Ptr _groupInfo)
{
    GROUP_LOG(INFO) << LOG_DESC("removeGroupNodeList") << printGroupInfo(_groupInfo);
    std::map<std::string, std::set<std::string>> groupToUnreachableNodes;
    std::set<std::string> unreachableNodes;
    auto nodeList = _groupInfo->nodeInfos();
    for (auto const& node : nodeList)
    {
        unreachableNodes.insert(node.second->nodeName());
    }
    groupToUnreachableNodes[_groupInfo->groupID()] = unreachableNodes;
    removeGroupBlockInfo(groupToUnreachableNodes);
    removeUnreachableNodeService(groupToUnreachableNodes);
}

bool GroupManager::shouldRebuildNodeService(
    std::string const& _groupID, bcos::group::ChainNodeInfo::Ptr _nodeInfo)
{
    auto const& nodeAppName = _nodeInfo->nodeName();
    if (!m_nodeServiceList.count(_groupID) || !m_nodeServiceList[_groupID].count(nodeAppName))
    {
        return true;
    }
    auto nodeInfo = m_groupInfos.at(_groupID)->nodeInfo(nodeAppName);
    // update the compatibilityVersion(Note: the compatibility version maybe updated in-runtime)
    if (nodeInfo->compatibilityVersion() != _nodeInfo->compatibilityVersion())
    {
        GROUP_LOG(INFO) << LOG_DESC("update compatibilityVersion to ")
                        << _nodeInfo->compatibilityVersion();
        nodeInfo->setCompatibilityVersion(_nodeInfo->compatibilityVersion());
    }
    // check the serviceInfo
    auto const& originServiceInfo = nodeInfo->serviceInfo();
    auto const& serviceInfo = _nodeInfo->serviceInfo();
    if (originServiceInfo.size() != serviceInfo.size())
    {
        GROUP_LOG(INFO) << LOG_DESC("shouldRebuildNodeService for serviceInfo changed");
        return true;
    }
    for (auto const& it : serviceInfo)
    {
        if (!originServiceInfo.count(it.first) || originServiceInfo.at(it.first) != it.second)
        {
            GROUP_LOG(INFO) << LOG_DESC("shouldRebuildNodeService for serviceInfo changed");
            return true;
        }
    }
    return false;
}

bool GroupManager::updateNodeService(
    std::string const& _groupID, ChainNodeInfo::Ptr _nodeInfo, bool _enforceUpdate)
{
    UpgradableGuard l(x_nodeServiceList);
    auto const& nodeAppName = _nodeInfo->nodeName();
    if (!_enforceUpdate && !shouldRebuildNodeService(_groupID, _nodeInfo))
    {
        return false;
    }
    // a started node
    auto nodeService = m_nodeServiceFactory->buildNodeService(m_chainID, _groupID, _nodeInfo);
    if (!nodeService)
    {
        return false;
    }
    // fetch blockNumber to the node
    initNodeInfo(_groupID, _nodeInfo->nodeName(), nodeService);
    UpgradeGuard ul(l);
    m_nodeServiceList[_groupID][nodeAppName] = nodeService;
    auto groupInfo = m_groupInfos[_groupID];
    // will cover the old NodeInfo
    groupInfo->appendNodeInfo(_nodeInfo);
    m_groupInfoNotifier(groupInfo);
    GROUP_LOG(INFO) << LOG_DESC("buildNodeService for the started new node")
                    << printNodeInfo(_nodeInfo) << printGroupInfo(groupInfo);
    return true;
}

bcos::protocol::BlockNumber GroupManager::getBlockNumberByGroup(const std::string& _groupID)
{
    ReadGuard l(x_groupBlockInfos);
    if (!m_groupBlockInfos.count(_groupID))
    {
        return -1;
    }

    return m_groupBlockInfos.at(_groupID);
}

NodeService::Ptr GroupManager::selectNode(std::string_view _groupID) const
{
    auto nodeName = selectNodeByBlockNumber(_groupID);
    if (nodeName.size() == 0)
    {
        return selectNodeRandomly(_groupID);
    }
    return queryNodeService(_groupID, nodeName);
}

std::string GroupManager::selectNodeByBlockNumber(std::string_view _groupID) const
{
    ReadGuard l(x_groupBlockInfos);

    auto it = m_nodesWithLatestBlockNumber.find(_groupID);
    if (it == m_nodesWithLatestBlockNumber.end() || it->second.size() == 0)
    {
        return "";
    }
    srand(utcTime());

    auto nodeListIt = m_nodesWithLatestBlockNumber.find(_groupID);
    auto const& nodesList = nodeListIt->second;
    auto selectNodeIndex = rand() % nodesList.size();
    auto nodeIt = nodesList.begin();
    if (selectNodeIndex > 0)
    {
        std::advance(nodeIt, selectNodeIndex);
    }
    return *nodeIt;
}

NodeService::Ptr GroupManager::selectNodeRandomly(std::string_view _groupID) const
{
    ReadGuard l(x_nodeServiceList);
    if (!m_groupInfos.count(_groupID))
    {
        return nullptr;
    }
    if (!m_nodeServiceList.count(_groupID))
    {
        return nullptr;
    }

    auto it = m_groupInfos.find(_groupID);
    auto const& groupInfo = it->second;
    auto const& nodeInfos = groupInfo->nodeInfos();
    for (auto const& it : nodeInfos)
    {
        auto const& node = it.second;
        auto serviceIt = m_nodeServiceList.find(_groupID);
        if (serviceIt != m_nodeServiceList.end() && !serviceIt->second.empty())
        {
            auto nodeService = serviceIt->second.at(node->nodeName());
            if (nodeService)
            {
                return nodeService;
            }
        }
    }
    return nullptr;
}

NodeService::Ptr GroupManager::queryNodeService(
    std::string_view _groupID, std::string_view _nodeName) const
{
    ReadGuard l(x_nodeServiceList);
    auto it = m_nodeServiceList.find(_groupID);
    if (it != m_nodeServiceList.end())
    {
        auto const& serviceList = it->second;
        auto it2 = serviceList.find(_nodeName);
        if (it2 != serviceList.end())
        {
            return it2->second;
        }
    }
    return nullptr;
}

NodeService::Ptr GroupManager::getNodeService(
    std::string_view _groupID, std::string_view _nodeName) const
{
    if (_nodeName.size() > 0)
    {
        return queryNodeService(_groupID, _nodeName);
    }
    return selectNode(_groupID);
}

void GroupManager::initNodeInfo(
    std::string const& _groupID, std::string const& _nodeName, NodeService::Ptr _nodeService)
{
    GROUP_LOG(INFO) << LOG_DESC("initNodeInfo") << LOG_KV("group", _groupID)
                    << LOG_KV("nodeName", _nodeName);
    auto ledger = _nodeService->ledger();
    auto self = std::weak_ptr<GroupManager>(shared_from_this());
    ledger->asyncGetBlockNumber(
        [self, _groupID, _nodeName](Error::Ptr _error, BlockNumber _blockNumber) {
            if (_error)
            {
                GROUP_LOG(WARNING)
                    << LOG_DESC("initNodeInfo error") << LOG_KV("code", _error->errorCode())
                    << LOG_KV("msg", _error->errorMessage()) << LOG_KV("groupID", _groupID)
                    << LOG_KV("nodeName", _nodeName);
                return;
            }
            try
            {
                auto groupMgr = self.lock();
                if (!groupMgr)
                {
                    return;
                }
                groupMgr->updateGroupBlockInfo(_groupID, _nodeName, _blockNumber);
                if (groupMgr->m_blockNumberNotifier)
                {
                    groupMgr->m_blockNumberNotifier(_groupID, _nodeName, _blockNumber);
                }
                GROUP_LOG(INFO) << LOG_DESC("initNodeInfo success") << LOG_KV("group", _groupID)
                                << LOG_KV("nodeName", _nodeName) << LOG_KV("number", _blockNumber);
            }
            catch (std::exception const& e)
            {
                GROUP_LOG(WARNING) << LOG_DESC("initNodeInfo exception")
                                   << LOG_KV("group", _groupID) << LOG_KV("nodeName", _nodeName)
                                   << LOG_KV("error", boost::diagnostic_information(e));
            }
        });
}

void GroupManager::removeUnreachableNodeService(
    std::map<std::string, std::set<std::string>> const& _unreachableNodes)
{
    WriteGuard l(x_nodeServiceList);
    for (auto const& it : _unreachableNodes)
    {
        auto groupID = it.first;
        auto& groupInfo = m_groupInfos[groupID];
        if (!m_nodeServiceList.count(groupID))
        {
            continue;
        }
        auto const& nodeList = it.second;
        for (auto const& node : nodeList)
        {
            GROUP_LOG(INFO) << LOG_DESC("GroupManager: removeUnreachablNodeService")
                            << LOG_KV("group", groupID) << LOG_KV("node", node);
            m_nodeServiceList[groupID].erase(node);
            groupInfo->removeNodeInfo(node);
        }
        if (m_nodeServiceList[groupID].size() == 0)
        {
            m_nodeServiceList.erase(groupID);
        }
    }
}
void GroupManager::removeGroupBlockInfo(
    std::map<std::string, std::set<std::string>> const& _unreachableNodes)
{
    WriteGuard l(x_groupBlockInfos);
    for (auto const& it : _unreachableNodes)
    {
        auto group = it.first;
        if (!m_nodesWithLatestBlockNumber.count(group))
        {
            m_groupBlockInfos.erase(group);
            continue;
        }
        if (!m_groupBlockInfos.count(group))
        {
            m_nodesWithLatestBlockNumber.erase(group);
            continue;
        }
        auto const& nodeList = it.second;
        for (auto const& node : nodeList)
        {
            m_nodesWithLatestBlockNumber[group].erase(node);
        }
        if (m_nodesWithLatestBlockNumber[group].size() == 0)
        {
            m_groupBlockInfos.erase(group);
            m_nodesWithLatestBlockNumber.erase(group);
        }
    }
}