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
#include "bcos-framework/protocol/CommonError.h"
#include "bcos-framework/protocol/ServiceDesc.h"
#include "bcos-gateway/Common.h"
#include "bcos-gateway/libnetwork/Message.h"
#include "bcos-tars-protocol/client/FrontServiceClient.h"
#include "fisco-bcos-tars-service/Common/TarsUtils.h"
#include <bcos-task/Wait.h>
using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::gateway;
using namespace bcos::front;
using namespace bcos::crypto;

LocalRouterTable::LocalRouterTable(bcos::crypto::KeyFactory::Ptr _keyFactory)
  : m_keyFactory(_keyFactory)
{}

LocalRouterTable::~LocalRouterTable() = default;

LocalRouterTable::GroupNodeListType LocalRouterTable::nodeList() const
{
    return m_nodeList.read();
}

FrontServiceInfo::Ptr LocalRouterTable::getFrontService(
    const std::string& _groupID, NodeIDPtr _nodeID) const
{
    auto const& nodeList = m_nodeList.read();
    auto it = nodeList.find(_groupID);
    if (it == nodeList.end())
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
    auto const& nodeList = m_nodeList.read();
    auto it = nodeList.find(_groupID);
    if (it == nodeList.end())
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
    auto const& nodeList = m_nodeList.read();
    auto it = nodeList.find(_groupID);
    if (it == nodeList.end())
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
    auto const& snapshot = m_nodeList.read();
    for (auto const& it : snapshot)
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
    auto frontServiceInfo =
        std::make_shared<FrontServiceInfo>(nodeIDStr, _frontService, _type, nullptr);
    frontServiceInfo->setProtocolInfo(_protocolInfo);

    bool inserted = true;
    m_nodeList.update([&](GroupNodeListType& nodeList) {
        auto groupIt = nodeList.find(_groupID);
        if (groupIt != nodeList.end())
        {
            auto nodeIt = groupIt->second.find(nodeIDStr);
            if (nodeIt != groupIt->second.end() && nodeIt->second->nodeType() == _type)
            {
                inserted = false;
                return;
            }
        }
        nodeList[_groupID][nodeIDStr] = frontServiceInfo;
    });
    if (!inserted)
    {
        ROUTER_LOG(INFO) << LOG_DESC("insertNode: the node has already existed")
                         << LOG_KV("groupID", _groupID) << LOG_KV("nodeID", nodeIDStr)
                         << LOG_KV("nodeType", _type);
        return false;
    }
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
    bool removed = false;
    m_nodeList.update([&](GroupNodeListType& nodeList) {
        auto groupIt = nodeList.find(_groupID);
        if (groupIt == nodeList.end() || groupIt->second.find(_nodeID) == groupIt->second.end())
        {
            return;
        }
        removed = true;
        groupIt->second.erase(_nodeID);
        if (groupIt->second.empty())
        {
            nodeList.erase(groupIt);
        }
    });
    if (!removed)
    {
        ROUTER_LOG(INFO) << LOG_DESC("removeNode: the node is not registered")
                         << LOG_KV("groupID", _groupID) << LOG_KV("nodeID", _nodeID);
        return false;
    }
    ROUTER_LOG(INFO) << LOG_DESC("removeNode") << LOG_KV("groupID", _groupID)
                     << LOG_KV("nodeID", _nodeID);
    return true;
}

bool LocalRouterTable::updateGroupNodeInfos(bcos::group::GroupInfo::Ptr _groupInfo)
{
    auto const& groupID = _groupInfo->groupID();
    auto const& nodeInfos = _groupInfo->nodeInfos();

    // Decide what to insert OUTSIDE the write critical section: building a front-service proxy is
    // not cheap, and with a snapshot table the mutation itself is a short clone-and-publish.
    std::vector<std::pair<std::string, FrontServiceInfo::Ptr>> pending;
    {
        auto const& nodeList = m_nodeList.read();
        for (auto const& it : nodeInfos)
        {
            auto const& nodeInfo = it.second;
            auto const& nodeID = nodeInfo->nodeID();
            // already registered with the same type and protocol range
            auto groupIt = nodeList.find(groupID);
            if (groupIt != nodeList.end())
            {
                auto nodeIt = groupIt->second.find(nodeID);
                if (nodeIt != groupIt->second.end())
                {
                    auto const& current = nodeIt->second;
                    auto protocol = nodeInfo->nodeProtocol();
                    auto currentProtocol = current->protocolInfo();
                    if (current->nodeType() == nodeInfo->nodeType() &&
                        protocol->minVersion() == currentProtocol->minVersion() &&
                        protocol->maxVersion() == currentProtocol->maxVersion())
                    {
                        continue;
                    }
                }
            }

            auto serviceName = nodeInfo->serviceName(bcos::protocol::FRONT);
            if (serviceName.empty())
            {
                continue;
            }

            // TODO:: tars
            auto frontPrx = bcostars::createServantProxy<bcostars::FrontServicePrx>(serviceName);
            auto frontClient =
                std::make_shared<bcostars::FrontServiceClient>(frontPrx, m_keyFactory);
            auto frontServiceInfo = std::make_shared<FrontServiceInfo>(
                nodeInfo->nodeID(), frontClient, nodeInfo->nodeType(), frontPrx);
            frontServiceInfo->setProtocolInfo(nodeInfo->nodeProtocol());
            pending.emplace_back(nodeID, std::move(frontServiceInfo));
        }
    }

    if (pending.empty())
    {
        return false;
    }

    m_nodeList.update([&](GroupNodeListType& nodeList) {
        for (auto& [nodeID, info] : pending)
        {
            nodeList[groupID][nodeID] = info;
            ROUTER_LOG(INFO) << LOG_DESC("updateGroupNodeInfos: insert frontService for the node")
                             << LOG_KV("nodeID", nodeID)
                             << LOG_KV("minVersion", info->protocolInfo()->minVersion())
                             << LOG_KV("maxVersion", info->protocolInfo()->maxVersion());
        }
    });
    return true;
}

bool LocalRouterTable::eraseUnreachableNodes()
{
    bool updated = false;
    m_nodeList.update([&](GroupNodeListType& nodeList) {
        for (auto it = nodeList.begin(); it != nodeList.end();)
        {
            auto& nodesInfo = it->second;
            for (auto pFrontService = nodesInfo.begin(); pFrontService != nodesInfo.end();)
            {
                if (pFrontService->second->unreachable())
                {
                    ROUTER_LOG(INFO) << LOG_DESC("remove FrontService for unreachable")
                                     << LOG_KV("node", pFrontService->first);
                    pFrontService = nodesInfo.erase(pFrontService);
                    updated = true;
                    continue;
                }
                pFrontService++;
            }
            if (nodesInfo.empty())
            {
                it = nodeList.erase(it);
                updated = true;
                continue;
            }
            it++;
        }
    });
    return updated;
}

bool LocalRouterTable::broadcastMsg(uint16_t _nodeType, const std::string& _groupID,
    uint16_t _moduleID, NodeIDPtr _srcNodeID, std::shared_ptr<Message> _msg) const
{
    auto frontServiceList = getGroupFrontServiceList(_groupID);
    if (frontServiceList.empty())
    {
        return false;
    }
    auto srcNodeIDHex = _srcNodeID->hex();
    // zero-copy: each dispatch task below owns the Message, so the payload view into it
    // stays valid for the whole (possibly deferred) per-front dispatch
    auto payloadSize = _msg->payload().size();
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
                          << LOG_KV("moduleID", _moduleID) << LOG_KV("payloadSize", payloadSize)
                          << LOG_KV("dst", dstNodeID);
        task::wait([](bcos::front::FrontServiceInterface::Ptr _frontService, std::string _groupID,
                       uint16_t _moduleID, NodeIDPtr _srcNodeID, std::shared_ptr<Message> _msg,
                       std::string _dstNodeID) -> task::Task<void> {
            auto error =
                co_await _frontService->onReceiveMessage(_groupID, _srcNodeID, _msg->payload());
            if (error)
            {
                GATEWAY_LOG(ERROR)
                    << LOG_DESC("ROUTER_LOG error") << LOG_KV("groupID", _groupID)
                    << LOG_KV("moduleID", _moduleID) << LOG_KV("src", _srcNodeID->hex())
                    << LOG_KV("dst", _dstNodeID) << LOG_KV("code", error->errorCode())
                    << LOG_KV("msg", error->errorMessage());
            }
        }(frontService, _groupID, _moduleID, _srcNodeID, _msg, dstNodeID));
    }
    return true;
}


// send message to the local nodes
task::Task<bcos::Error::Ptr> LocalRouterTable::sendMessage(
    std::string _groupID, NodeIDPtr _srcNodeID, NodeIDPtr _dstNodeID, bytesConstRef _payload)
{
    auto frontServiceInfo = getFrontService(_groupID, _dstNodeID);
    if (!frontServiceInfo)
    {
        co_return BCOS_ERROR_PTR(CommonError::NotFoundFrontServiceSendMsg,
            "could not find a gateway to send this message, groupID:" + _groupID +
                " ,dstNodeID:" + _dstNodeID->hex());
    }
    co_return co_await frontServiceInfo->frontService()->onReceiveMessage(
        _groupID, std::move(_srcNodeID), _payload);
}
