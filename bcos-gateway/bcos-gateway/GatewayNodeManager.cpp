
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
 * @file GatewayNodeManager.cpp
 * @author: octopus
 * @date 2021-05-13
 */
#include <bcos-framework/interfaces/protocol/CommonError.h>
#include <bcos-framework/interfaces/protocol/ServiceDesc.h>
#include <bcos-framework/libutilities/DataConvertUtility.h>
#include <bcos-gateway/GatewayNodeManager.h>
#include <json/json.h>

using namespace std;
using namespace bcos;
using namespace gateway;
using namespace bcos::protocol;
using namespace bcos::group;

/**
 * @brief: register FrontService
 * @param _groupID: groupID
 * @param _nodeID: nodeID
 * @param _frontServiceInterface: FrontService
 * @return void
 */
bool GatewayNodeManager::registerFrontService(const std::string& _groupID,
    bcos::crypto::NodeIDPtr _nodeID, bcos::front::FrontServiceInterface::Ptr _frontServiceInterface)
{
    bool isExist = false;
    {
        WriteGuard l(x_frontServiceInfos);
        auto it = m_frontServiceInfos.find(_groupID);
        if (it != m_frontServiceInfos.end())
        {
            auto innerIt = it->second.find(_nodeID->hex());
            isExist = (innerIt != it->second.end());
        }

        if (!isExist)
        {
            m_frontServiceInfos[_groupID][_nodeID->hex()] =
                std::make_shared<FrontServiceInfo>(_frontServiceInterface, nullptr);
            increaseSeq();
        }
    }

    if (!isExist)
    {
        NODE_MANAGER_LOG(INFO) << LOG_DESC("registerFrontService") << LOG_KV("groupID", _groupID)
                               << LOG_KV("nodeID", _nodeID->hex())
                               << LOG_KV("statusSeq", m_statusSeq);
    }
    else
    {
        NODE_MANAGER_LOG(WARNING) << LOG_DESC("registerFrontService front service already exist")
                                  << LOG_KV("groupID", _groupID)
                                  << LOG_KV("nodeID", _nodeID->hex());
    }

    return !isExist;
}

/**
 * @brief: unregister FrontService
 * @param _groupID: groupID
 * @param _nodeID: nodeID
 * @return bool
 */
bool GatewayNodeManager::unregisterFrontService(
    const std::string& _groupID, bcos::crypto::NodeIDPtr _nodeID)
{
    bool isOK = false;
    {
        WriteGuard l(x_frontServiceInfos);
        auto it = m_frontServiceInfos.find(_groupID);
        if (it != m_frontServiceInfos.end())
        {
            auto innerIt = it->second.find(_nodeID->hex());
            if (innerIt != it->second.end())
            {
                it->second.erase(innerIt);
                increaseSeq();
                isOK = true;
                if (it->second.empty())
                {
                    m_frontServiceInfos.erase(it);
                }
            }
        }
    }

    if (isOK)
    {
        NODE_MANAGER_LOG(INFO) << LOG_DESC("unregisterFrontService") << LOG_KV("groupID", _groupID)
                               << LOG_KV("nodeID", _nodeID->hex())
                               << LOG_KV("statusSeq", m_statusSeq);
    }
    else
    {
        NODE_MANAGER_LOG(WARNING) << LOG_DESC("unregisterFrontService front service not exist")
                                  << LOG_KV("groupID", _groupID)
                                  << LOG_KV("nodeID", _nodeID->hex());
    }

    return isOK;
}

bcos::front::FrontServiceInterface::Ptr
GatewayNodeManager::queryFrontServiceInterfaceByGroupIDAndNodeID(
    const std::string& _groupID, bcos::crypto::NodeIDPtr _nodeID)
{
    bcos::front::FrontServiceInterface::Ptr frontServiceInterface = nullptr;
    {
        ReadGuard l(x_frontServiceInfos);
        auto it = m_frontServiceInfos.find(_groupID);
        if (it != m_frontServiceInfos.end())
        {
            auto innerIt = it->second.find(_nodeID->hex());
            if (innerIt != it->second.end())
            {
                frontServiceInterface = innerIt->second->frontService();
            }
        }
    }

    if (!frontServiceInterface)
    {
        NODE_MANAGER_LOG(WARNING) << LOG_DESC(
                                         "queryFrontServiceInterfaceByGroupIDA"
                                         "ndNodeID front service of the node"
                                         " not exist")
                                  << LOG_KV("groupID", _groupID)
                                  << LOG_KV("nodeID", _nodeID->hex());
    }

    return frontServiceInterface;
}

std::set<bcos::front::FrontServiceInterface::Ptr>
GatewayNodeManager::queryFrontServiceInterfaceByGroupID(const std::string& _groupID)
{
    std::set<bcos::front::FrontServiceInterface::Ptr> frontServiceInterfaces;
    {
        ReadGuard l(x_frontServiceInfos);
        auto it = m_frontServiceInfos.find(_groupID);
        if (it != m_frontServiceInfos.end())
        {
            for (const auto& innerIt : it->second)
            {
                frontServiceInterfaces.insert(
                    frontServiceInterfaces.begin(), innerIt.second->frontService());
            }
        }
    }

    if (frontServiceInterfaces.empty())
    {
        NODE_MANAGER_LOG(WARNING) << LOG_DESC(
                                         "queryFrontServiceInterfaceByGroupID front service of the "
                                         "group not exist")
                                  << LOG_KV("groupID", _groupID);
    }

    return frontServiceInterfaces;
}

void GatewayNodeManager::onReceiveStatusSeq(
    const P2pID& _p2pID, uint32_t _statusSeq, bool& _statusSeqChanged)
{
    _statusSeqChanged = true;
    {
        std::lock_guard<std::mutex> l(x_peerGatewayNodes);

        auto it = m_p2pID2Seq.find(_p2pID);
        if (it != m_p2pID2Seq.end())
        {
            _statusSeqChanged = (_statusSeq != it->second);
        }
    }

    if (_statusSeqChanged)
    {
        NODE_MANAGER_LOG(INFO) << LOG_DESC("onReceiveStatusSeq") << LOG_KV("p2pid", _p2pID)
                               << LOG_KV("statusSeq", _statusSeq)
                               << LOG_KV("seqChanged", _statusSeqChanged);
    }
    else
    {
        NODE_MANAGER_LOG(DEBUG) << LOG_DESC("onReceiveStatusSeq") << LOG_KV("p2pid", _p2pID)
                                << LOG_KV("statusSeq", _statusSeq)
                                << LOG_KV("seqChanged", _statusSeqChanged);
    }
}

void GatewayNodeManager::notifyNodeIDs2FrontService()
{
    std::unordered_map<std::string, std::unordered_map<std::string, FrontServiceInfo::Ptr>>
        frontServiceInfos;
    {
        ReadGuard l(x_frontServiceInfos);
        frontServiceInfos = m_frontServiceInfos;
    }

    for (auto const& groupEntry : frontServiceInfos)
    {
        const auto& groupID = groupEntry.first;

        std::shared_ptr<crypto::NodeIDs> nodeIDs = std::make_shared<crypto::NodeIDs>();

        queryNodeIDsByGroupID(groupID, *nodeIDs);

        NODE_MANAGER_LOG(INFO) << LOG_DESC("notifyNodeIDs2FrontService")
                               << LOG_KV("groupID", groupID)
                               << LOG_KV("nodeCount", nodeIDs->size());

        for (const auto& frontServiceEntry : groupEntry.second)
        {
            frontServiceEntry.second->frontService()->onReceiveNodeIDs(
                groupID, nodeIDs, [](Error::Ptr _error) {
                    if (!_error)
                    {
                        return;
                    }
                    NODE_MANAGER_LOG(WARNING)
                        << LOG_DESC("notifyNodeIDs2FrontService onReceiveNodeIDs callback")
                        << LOG_KV("codeCode", _error->errorCode())
                        << LOG_KV("codeMessage", _error->errorMessage());
                });
        }
    }
    return;
}

void GatewayNodeManager::showAllPeerGatewayNodeIDs()
{
    for (auto it = m_peerGatewayNodes.begin(); it != m_peerGatewayNodes.end(); ++it)
    {
        NODE_MANAGER_LOG(INFO) << LOG_DESC("peerGatewayNodes") << LOG_KV("groupID", it->first)
                               << LOG_KV("nodeCount", it->second.size());
        for (auto innerIt = it->second.begin(); innerIt != it->second.end(); ++innerIt)
        {
            NODE_MANAGER_LOG(INFO) << LOG_DESC("peerGatewayNodes") << LOG_KV("groupID", it->first)
                                   << LOG_KV("nodeID", innerIt->first)
                                   << LOG_KV("gatewayCount", innerIt->second.size());
            for (auto innerIt2 = innerIt->second.begin(); innerIt2 != innerIt->second.end();
                 ++innerIt2)
            {
                NODE_MANAGER_LOG(INFO)
                    << LOG_DESC("peerGatewayNodes") << LOG_KV("groupID", it->first)
                    << LOG_KV("nodeID", innerIt->first) << LOG_KV("p2pID", *innerIt2);
            }
        }
    }
}

void GatewayNodeManager::updateNodeIDs(const P2pID& _p2pID, uint32_t _seq,
    const std::unordered_map<std::string, std::set<std::string>>& _nodeIDsMap)
{
    NODE_MANAGER_LOG(INFO) << LOG_DESC("updateNodeIDs") << LOG_KV("p2pid", _p2pID)
                           << LOG_KV("statusSeq", _seq);

    {
        std::lock_guard<std::mutex> l(x_peerGatewayNodes);
        // remove peer nodeIDs info first
        removeNodeIDsByP2PID(_p2pID);
        // insert current nodeIDs info
        for (const auto& nodeIDs : _nodeIDsMap)
        {
            for (const auto& nodeID : nodeIDs.second)
            {
                m_peerGatewayNodes[nodeIDs.first][nodeID].insert(_p2pID);
            }
        }
        // update seq
        m_p2pID2Seq[_p2pID] = _seq;
        showAllPeerGatewayNodeIDs();
    }
    updateNodeIDInfo(_p2pID, _nodeIDsMap);
    // notify nodeIDs to front service
    notifyNodeIDs2FrontService();
}

void GatewayNodeManager::removeNodeIDsByP2PID(const std::string& _p2pID)
{
    // remove all nodeIDs info belong to p2pID
    for (auto it = m_peerGatewayNodes.begin(); it != m_peerGatewayNodes.end();)
    {
        for (auto innerIt = it->second.begin(); innerIt != it->second.end();)
        {
            for (auto innerIt2 = innerIt->second.begin(); innerIt2 != innerIt->second.end();)
            {
                if (*innerIt2 == _p2pID)
                {
                    innerIt2 = innerIt->second.erase(innerIt2);
                }
                else
                {
                    ++innerIt2;
                }
            }  // for (auto innerIt2

            if (innerIt->second.empty())
            {
                innerIt = it->second.erase(innerIt);
            }
            else
            {
                ++innerIt;
            }
        }  // for (auto innerIt

        if (it->second.empty())
        {
            it = m_peerGatewayNodes.erase(it);
        }
        else
        {
            ++it;
        }
    }  // for (auto it
    removeNodeIDInfo(_p2pID);
    showAllPeerGatewayNodeIDs();
}

bool GatewayNodeManager::parseReceivedJson(const std::string& _json, uint32_t& statusSeq,
    std::unordered_map<std::string, std::set<std::string>>& nodeIDsMap)
{
    /*
    sample:
    {"statusSeq":1,"nodeInfoList":[{"groupID":"group1","nodeIDs":["a0","b0","c0"]},{"groupID":"group2","nodeIDs":["a1","b1","c1"]},{"groupID":"group3","nodeIDs":["a2","b2","c2"]}]}
    */
    Json::Value root;
    Json::Reader jsonReader;

    try
    {
        if (!jsonReader.parse(_json, root))
        {
            NODE_MANAGER_LOG(ERROR)
                << "parseReceivedJson unable to parse this json" << LOG_KV("json:", _json);
            return false;
        }

        statusSeq = root["statusSeq"].asUInt();
        auto jsonArraySize = root["nodeInfoList"].size();

        for (unsigned int i = 0; i < jsonArraySize; i++)
        {
            auto jNode = root["nodeInfoList"][i];

            // groupID
            std::string groupID = jNode["groupID"].asString();
            // nodeID set
            std::set<std::string> nodeIDsSet;
            auto nodeIDsSize = jNode["nodeIDs"].size();
            for (unsigned int j = 0; j < nodeIDsSize; j++)
            {
                auto nodeID = jNode["nodeIDs"][j].asString();
                nodeIDsSet.insert(nodeID);
            }

            nodeIDsMap[groupID] = nodeIDsSet;
        }

        NODE_MANAGER_LOG(INFO) << LOG_DESC("parseReceivedJson ") << LOG_KV("statusSeq", statusSeq)
                               << LOG_KV("json", _json);
        return true;
    }
    catch (const std::exception& e)
    {
        NODE_MANAGER_LOG(ERROR) << LOG_DESC(
            "parseReceivedJson error: " + boost::diagnostic_information(e));
        return false;
    }
}

void GatewayNodeManager::onReceiveNodeIDs(const P2pID& _p2pID, const std::string& _nodeIDsJson)
{
    // parser info json first
    uint32_t statusSeq;
    std::unordered_map<std::string, std::set<std::string>> nodeIDsMap;
    if (parseReceivedJson(_nodeIDsJson, statusSeq, nodeIDsMap))
    {
        updateNodeIDs(_p2pID, statusSeq, nodeIDsMap);
    }
}

void GatewayNodeManager::onRequestNodeIDs(std::string& _nodeIDsJson)
{
    // groupID => nodeIDs list
    std::unordered_map<std::string, std::set<std::string>> localGroup2NodeIDs;
    uint32_t seq = 0;
    {
        ReadGuard l(x_frontServiceInfos);
        seq = statusSeq();
        for (const auto& frontServiceInfos : m_frontServiceInfos)
        {
            for (const auto& nodeID2FrontServiceInterface : frontServiceInfos.second)
            {
                localGroup2NodeIDs[frontServiceInfos.first].insert(
                    nodeID2FrontServiceInterface.first);
            }
        }
    }

    // generator json first
    try
    {
        Json::Value jArray = Json::Value(Json::arrayValue);
        for (const auto& group2NodeIDs : localGroup2NodeIDs)
        {
            Json::Value jNode;
            jNode["groupID"] = group2NodeIDs.first;
            jNode["nodeIDs"] = Json::Value(Json::arrayValue);
            for (const auto& nodeID : group2NodeIDs.second)
            {
                jNode["nodeIDs"].append(nodeID);
            }
            jArray.append(jNode);
        }

        Json::Value jResp;
        jResp["statusSeq"] = seq;
        jResp["nodeInfoList"] = jArray;

        Json::FastWriter writer;
        _nodeIDsJson = writer.write(jResp);

        NODE_MANAGER_LOG(INFO) << LOG_DESC("onRequestNodeIDs ") << LOG_KV("seq", seq)
                               << LOG_KV("json", _nodeIDsJson);
    }
    catch (const std::exception& e)
    {
        NODE_MANAGER_LOG(ERROR) << LOG_DESC(
            "onRequestNodeIDs error: " + boost::diagnostic_information(e));
    }
}

void GatewayNodeManager::onRemoveNodeIDs(const P2pID& _p2pID)
{
    NODE_MANAGER_LOG(INFO) << LOG_DESC("onRemoveNodeIDs") << LOG_KV("p2pid", _p2pID);

    {
        std::lock_guard<std::mutex> l(x_peerGatewayNodes);
        // remove statusSeq info
        removeNodeIDsByP2PID(_p2pID);
        m_p2pID2Seq.erase(_p2pID);
        showAllPeerGatewayNodeIDs();
    }

    // notify nodeIDs to front service
    notifyNodeIDs2FrontService();
}

bool GatewayNodeManager::queryP2pIDs(
    const std::string& _groupID, const std::string& _nodeID, std::set<P2pID>& _p2pIDs)
{
    std::lock_guard<std::mutex> l(x_peerGatewayNodes);

    auto it = m_peerGatewayNodes.find(_groupID);
    if (it == m_peerGatewayNodes.end())
    {
        return false;
    }

    auto innerIt = it->second.find(_nodeID);
    if (innerIt == it->second.end())
    {
        return false;
    }

    _p2pIDs.insert(innerIt->second.begin(), innerIt->second.end());

    return true;
}

bool GatewayNodeManager::queryP2pIDsByGroupID(const std::string& _groupID, std::set<P2pID>& _p2pIDs)
{
    std::lock_guard<std::mutex> l(x_peerGatewayNodes);

    auto it = m_peerGatewayNodes.find(_groupID);
    if (it == m_peerGatewayNodes.end())
    {
        return false;
    }

    for (const auto& nodeMap : it->second)
    {
        _p2pIDs.insert(nodeMap.second.begin(), nodeMap.second.end());
    }

    return true;
}

void GatewayNodeManager::queryLocalNodeIDsByGroup(
    const std::string& _groupID, bcos::crypto::NodeIDs& _nodeIDs)
{
    ReadGuard l(x_frontServiceInfos);
    if (!m_frontServiceInfos.count(_groupID))
    {
        return;
    }
    for (auto const& item : m_frontServiceInfos[_groupID])
    {
        auto bytes = bcos::fromHexString(item.first);
        _nodeIDs.emplace_back(m_keyFactory->createKey(*bytes.get()));
    }
}

bool GatewayNodeManager::queryNodeIDsByGroupID(
    const std::string& _groupID, bcos::crypto::NodeIDs& _nodeIDs)
{
    queryLocalNodeIDsByGroup(_groupID, _nodeIDs);

    std::lock_guard<std::mutex> l(x_peerGatewayNodes);

    auto it = m_peerGatewayNodes.find(_groupID);
    if (it == m_peerGatewayNodes.end())
    {
        return false;
    }

    for (const auto& nodeEntry : it->second)
    {
        auto bytes = bcos::fromHexString(nodeEntry.first);
        if (bytes)
        {
            auto nodeID = m_keyFactory->createKey(*bytes.get());
            _nodeIDs.push_back(nodeID);
        }
    }
    return true;
}

FrontServiceInfo::Ptr GatewayNodeManager::queryLocalNodes(
    std::string const& _groupID, std::string const& _nodeID)
{
    ReadGuard l(x_frontServiceInfos);
    if (m_frontServiceInfos.count(_groupID) && m_frontServiceInfos[_groupID].count(_nodeID))
    {
        return m_frontServiceInfos[_groupID][_nodeID];
    }
    return nullptr;
}

std::unordered_map<std::string, FrontServiceInfo::Ptr> GatewayNodeManager::groupFrontServices(
    std::string const& _groupID)
{
    ReadGuard l(x_frontServiceInfos);
    if (!m_frontServiceInfos.count(_groupID))
    {
        return std::unordered_map<std::string, FrontServiceInfo::Ptr>();
    }
    return m_frontServiceInfos[_groupID];
}

void GatewayNodeManager::updateNodeIDInfo(std::string const& _p2pNodeID,
    std::unordered_map<std::string, std::set<std::string>> const& _nodeIDList)
{
    WriteGuard l(x_nodeIDInfo);
    m_nodeIDInfo[_p2pNodeID] = _nodeIDList;
}

void GatewayNodeManager::removeNodeIDInfo(std::string const& _p2pNodeID)
{
    UpgradableGuard l(x_nodeIDInfo);
    if (m_nodeIDInfo.count(_p2pNodeID))
    {
        UpgradeGuard ul(l);
        m_nodeIDInfo.erase(_p2pNodeID);
    }
}

std::unordered_map<std::string, std::set<std::string>> GatewayNodeManager::getLocalNodeIDInfo()
{
    std::unordered_map<std::string, std::set<std::string>> nodeIDInfo;
    ReadGuard l(x_frontServiceInfos);
    for (auto const& it : m_frontServiceInfos)
    {
        auto groupID = it.first;
        if (!nodeIDInfo.count(groupID))
        {
            nodeIDInfo[groupID] = std::set<std::string>();
        }
        auto const& infos = it.second;
        for (auto const& nodeIt : infos)
        {
            nodeIDInfo[groupID].insert(nodeIt.first);
        }
    }
    return nodeIDInfo;
}

std::unordered_map<std::string, std::set<std::string>> GatewayNodeManager::nodeIDInfo(
    std::string const& _p2pNodeID)
{
    // the local nodeID info
    if (_p2pNodeID == m_p2pNodeID)
    {
        return getLocalNodeIDInfo();
    }
    ReadGuard l(x_nodeIDInfo);
    if (m_nodeIDInfo.count(_p2pNodeID))
    {
        return m_nodeIDInfo[_p2pNodeID];
    }
    return std::unordered_map<std::string, std::set<std::string>>();
}
