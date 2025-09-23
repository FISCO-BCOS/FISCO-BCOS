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
 * @file LocalRouterTable.cpp
 * @author: octopus
 * @date 2021-12-29
 */
#include "LocalRouterTable.h"
#include "fisco-bcos-tars-service/Common/TarsUtils.h"
#include <bcos-framework/protocol/ServiceDesc.h>
#include <bcos-gateway/Common.h>
using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::gateway;
using namespace bcos::front;
using namespace bcos::crypto;

FrontServiceInfo::Ptr LocalRouterTable::getFrontService(
    const std::string& _groupID, NodeIDPtr _nodeID) const
{
    ReadGuard l(x_nodeList);
    auto it = m_nodeList.find(_groupID);
    if (it == m_nodeList.end())
    {
        return nullptr;
    }
    auto it2 = it->second.find(_nodeID->hex());
    if (it2 == it->second.end())
    {
        return nullptr;
    }
    return it2->second;
}

std::vector<FrontServiceInfo::Ptr> LocalRouterTable::getGroupFrontServiceList(
    const std::string& _groupID) const
{
    std::vector<FrontServiceInfo::Ptr> nodeServiceList;
    ReadGuard l(x_nodeList);
    auto it = m_nodeList.find(_groupID);
    if (it == m_nodeList.end())
    {
        return nodeServiceList;
    }
    nodeServiceList.reserve(it->second.size());
    for (auto const& item : it->second)
    {
        nodeServiceList.emplace_back(item.second);
    }
    return nodeServiceList;
}

void LocalRouterTable::getGroupNodeInfoList(
    GroupNodeInfo::Ptr _groupNodeInfo, const std::string& _groupID) const
{
    ReadGuard l(x_nodeList);
    auto it = m_nodeList.find(_groupID);
    if (it == m_nodeList.end())
    {
        return;
    }
    for (auto const& item : it->second)
    {
        _groupNodeInfo->appendNodeID(item.first);
        _groupNodeInfo->appendProtocol(item.second->protocolInfo());
    }
}

std::map<std::string, std::map<std::string, uint32_t>> LocalRouterTable::nodeListInfo() const
{
    std::map<std::string, std::map<std::string, uint32_t>> nodeList;
    ReadGuard l(x_nodeList);
    for (auto const& it : m_nodeList)
    {
        auto groupID = it.first;
        auto it2 = nodeList.find(groupID);
        if (it2 == nodeList.end())
        {
            nodeList[groupID] = std::map<std::string, uint32_t>();
        }
        it2 = nodeList.find(groupID);
        auto const& infos = it.second;
        for (auto const& nodeIt : infos)
        {
            auto nodeID = nodeIt.first;
            auto nodeType = nodeIt.second->nodeType();
            it2->second.insert(std::pair<std::string, uint32_t>(nodeID, nodeType));
        }
    }
    return nodeList;
}

/**
 * @brief: insert node
 * @param _groupID: groupID
 * @param _nodeID: nodeID
 * @param _frontService: FrontService
 */
bool LocalRouterTable::insertNode(const std::string& _groupID, NodeIDPtr _nodeID,
    bcos::protocol::NodeType _type, FrontServiceInterface::Ptr _frontService,
    bcos::protocol::ProtocolInfo::ConstPtr _protocolInfo)
{
    auto nodeIDStr = _nodeID->hex();
    UpgradableGuard l(x_nodeList);
    if (m_nodeList.count(_groupID) && m_nodeList[_groupID].count(nodeIDStr))
    {
        auto nodeType = (m_nodeList.at(_groupID).at(nodeIDStr))->nodeType();
        if (_type == nodeType)
        {
            ROUTER_LOG(INFO) << LOG_DESC("insertNode: the node has already existed")
                             << LOG_KV("groupID", _groupID) << LOG_KV("nodeID", nodeIDStr)
                             << LOG_KV("nodeType", _type);
            return false;
        }
    }
    auto frontServiceInfo =
        std::make_shared<FrontServiceInfo>(nodeIDStr, _frontService, _type, nullptr);
    frontServiceInfo->setProtocolInfo(_protocolInfo);
    UpgradeGuard ul(l);
    m_nodeList[_groupID][nodeIDStr] = frontServiceInfo;
    ROUTER_LOG(INFO) << LOG_DESC("insertNode") << LOG_KV("groupID", _groupID)
                     << LOG_KV("minVersion", _protocolInfo->minVersion())
                     << LOG_KV("maxVersion", _protocolInfo->maxVersion())
                     << LOG_KV("nodeID", nodeIDStr) << LOG_KV("nodeType", _type);
    return true;
}

/**
 * @brief: removeNode
 * @param _groupID: groupID
 * @param _nodeID: nodeID
 */
bool LocalRouterTable::removeNode(const std::string& _groupID, std::string const& _nodeID)
{
    UpgradableGuard l(x_nodeList);
    if (!m_nodeList.count(_groupID) || !m_nodeList[_groupID].count(_nodeID))
    {
        ROUTER_LOG(INFO) << LOG_DESC("removeNode: the node is not registered")
                         << LOG_KV("groupID", _groupID) << LOG_KV("nodeID", _nodeID);
        return false;
    }
    // erase the node from m_nodeList
    UpgradeGuard ul(l);
    (m_nodeList[_groupID]).erase(_nodeID);
    if (m_nodeList.at(_groupID).empty())
    {
        m_nodeList.erase(_groupID);
    }
    ROUTER_LOG(INFO) << LOG_DESC("removeNode") << LOG_KV("groupID", _groupID)
                     << LOG_KV("nodeID", _nodeID);
    return true;
}

bool LocalRouterTable::updateGroupNodeInfos(bcos::group::GroupInfo::Ptr _groupInfo)
{
    UpgradableGuard l(x_nodeList);
    auto const& groupID = _groupInfo->groupID();
    auto const& nodeInfos = _groupInfo->nodeInfos();
    bool frontServiceUpdated = false;
    for (auto const& it : nodeInfos)
    {
        auto const& nodeInfo = it.second;
        auto const& nodeID = nodeInfo->nodeID();
        // the node is registered
        if (m_nodeList.count(groupID) && m_nodeList[groupID].count(nodeID))
        {
            auto currentNodeInfo = m_nodeList.at(groupID).at(nodeID);
            auto nodeType = currentNodeInfo->nodeType();
            auto protocol = nodeInfo->nodeProtocol();
            auto currentProtocol = currentNodeInfo->protocolInfo();
            if (nodeType == nodeInfo->nodeType() &&
                (protocol->minVersion() == currentProtocol->minVersion()) &&
                (protocol->maxVersion() == currentProtocol->maxVersion()))
            {
                continue;
            }
        }
        // insert the new node
        auto serviceName = nodeInfo->serviceName(bcos::protocol::FRONT);
        if (serviceName.empty())
        {
            continue;
        }

        // TODO:: tars
        auto frontPrx = bcostars::createServantProxy<bcostars::FrontServicePrx>(serviceName);
        auto frontClient = std::make_shared<bcostars::FrontServiceClient>(frontPrx, m_keyFactory);

        UpgradeGuard ul(l);
        auto frontServiceInfo = std::make_shared<FrontServiceInfo>(
            nodeInfo->nodeID(), frontClient, nodeInfo->nodeType(), frontPrx);
        frontServiceInfo->setProtocolInfo(nodeInfo->nodeProtocol());
        m_nodeList[groupID][nodeID] = frontServiceInfo;
        ROUTER_LOG(INFO) << LOG_DESC("updateGroupNodeInfos: insert frontService for the node")
                         << LOG_KV("nodeID", nodeInfo->nodeID())
                         << LOG_KV("minVersion", nodeInfo->nodeProtocol()->minVersion())
                         << LOG_KV("maxVersion", nodeInfo->nodeProtocol()->maxVersion())
                         << LOG_KV("serviceName", serviceName) << printNodeInfo(nodeInfo);
        frontServiceUpdated = true;
    }
    return frontServiceUpdated;
}

bool LocalRouterTable::eraseUnreachableNodes()
{
    bool updated = false;
    UpgradableGuard l(x_nodeList);
    for (auto it = m_nodeList.begin(); it != m_nodeList.end();)
    {
        auto& nodesInfo = it->second;
        for (auto pFrontService = nodesInfo.begin(); pFrontService != nodesInfo.end();)
        {
            auto frontService = pFrontService->second;
            if (frontService->unreachable())
            {
                ROUTER_LOG(INFO) << LOG_DESC("remove FrontService for unreachable")
                                 << LOG_KV("node", pFrontService->first);
                UpgradeGuard ul(l);
                pFrontService = nodesInfo.erase(pFrontService);
                updated = true;
                continue;
            }
            pFrontService++;
        }
        if (nodesInfo.empty())
        {
            UpgradeGuard ul(l);
            it = m_nodeList.erase(it);
            updated = true;
            continue;
        }
        it++;
    }
    return updated;
}

bool LocalRouterTable::asyncBroadcastMsg(uint16_t _nodeType, const std::string& _groupID,
    uint16_t _moduleID, NodeIDPtr _srcNodeID, bytesConstRef _payload) const
{
    auto frontServiceList = getGroupFrontServiceList(_groupID);
    if (frontServiceList.empty())
    {
        return false;
    }
    auto srcNodeIDHex = _srcNodeID->hex();
    for (auto const& it : frontServiceList)
    {
        if (it->nodeID() == srcNodeIDHex)
        {
            continue;
        }
        // not expected to send message to the type of node
        if ((_nodeType & it->nodeType()) == 0)
        {
            continue;
        }
        auto frontService = it->frontService();
        const auto& dstNodeID = it->nodeID();
        ROUTER_LOG(TRACE) << LOG_BADGE(
                                 "LocalRouterTable: dispatcher broadcast-type message to node")
                          << LOG_KV("type", _nodeType) << LOG_KV("groupID", _groupID)
                          << LOG_KV("moduleID", _moduleID) << LOG_KV("payloadSize", _payload.size())
                          << LOG_KV("dst", dstNodeID);
        frontService->onReceiveMessage(_groupID, _srcNodeID, _payload,
            [_groupID, _moduleID, _srcNodeID, dstNodeID](Error::Ptr _error) {
                if (_error)
                {
                    GATEWAY_LOG(ERROR)
                        << LOG_DESC("ROUTER_LOG error") << LOG_KV("groupID", _groupID)
                        << LOG_KV("moduleID", _moduleID) << LOG_KV("src", _srcNodeID->hex())
                        << LOG_KV("dst", dstNodeID) << LOG_KV("code", _error->errorCode())
                        << LOG_KV("msg", _error->errorMessage());
                }
            });
    }
    return true;
}


// send message to the local nodes
bool LocalRouterTable::sendMessage(const std::string& _groupID, NodeIDPtr _srcNodeID,
    NodeIDPtr _dstNodeID, bytesConstRef _payload, ErrorRespFunc _errorRespFunc)
{
    auto frontServiceInfo = getFrontService(_groupID, _dstNodeID);
    if (!frontServiceInfo)
    {
        return false;
    }
    frontServiceInfo->frontService()->onReceiveMessage(
        _groupID, _srcNodeID, _payload, [_errorRespFunc](Error::Ptr _error) {
            if (_errorRespFunc)
            {
                _errorRespFunc(_error);
            }
        });
    return true;
}
