
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
#include "GatewayNodeManager.h"
#include <bcos-framework/interfaces/protocol/CommonError.h>
#include <bcos-framework/interfaces/protocol/ServiceDesc.h>
#include <bcos-framework/libutilities/DataConvertUtility.h>
#include <json/json.h>

using namespace std;
using namespace bcos;
using namespace gateway;
using namespace bcos::protocol;
using namespace bcos::group;
using namespace bcos::crypto;

GatewayNodeManager::GatewayNodeManager(P2pID const& _nodeID,
    std::shared_ptr<bcos::crypto::KeyFactory> _keyFactory, P2PInterface::Ptr _p2pInterface)
  : GatewayNodeManager(_keyFactory)
{
    m_p2pNodeID = _nodeID;
    m_p2pInterface = _p2pInterface;
    // SyncNodeSeq
    m_p2pInterface->registerHandlerByMsgType(MessageType::SyncNodeSeq,
        boost::bind(&GatewayNodeManager::onReceiveStatusSeq, this, boost::placeholders::_1,
            boost::placeholders::_2, boost::placeholders::_3));
    // RequestNodeIDs
    m_p2pInterface->registerHandlerByMsgType(MessageType::RequestNodeIDs,
        boost::bind(&GatewayNodeManager::onRequestNodeIDs, this, boost::placeholders::_1,
            boost::placeholders::_2, boost::placeholders::_3));
    // ResponseNodeIDs
    m_p2pInterface->registerHandlerByMsgType(MessageType::ResponseNodeIDs,
        boost::bind(&GatewayNodeManager::onResponseNodeIDs, this, boost::placeholders::_1,
            boost::placeholders::_2, boost::placeholders::_3));
    m_timer = std::make_shared<Timer>(SEQ_SYNC_PERIOD, "seqSync");
    // broadcast seq periodically
    m_timer->registerTimeoutHandler([this]() { broadcastStatusSeq(); });
}

void GatewayNodeManager::stop()
{
    if (m_p2pInterface)
    {
        m_p2pInterface->eraseHandlerByMsgType(MessageType::SyncNodeSeq);
        m_p2pInterface->eraseHandlerByMsgType(MessageType::RequestNodeIDs);
        m_p2pInterface->eraseHandlerByMsgType(MessageType::ResponseNodeIDs);
    }
    if (m_timer)
    {
        m_timer->stop();
    }
}

bool GatewayNodeManager::registerNode(const std::string& _groupID, bcos::crypto::NodeIDPtr _nodeID,
    bcos::front::FrontServiceInterface::Ptr _frontService)
{
    auto ret = m_localRouterTable->insertNode(_groupID, _nodeID, _frontService);
    if (ret)
    {
        increaseSeq();
    }
    return ret;
}

bool GatewayNodeManager::unregisterNode(
    const std::string& _groupID, bcos::crypto::NodeIDPtr _nodeID)
{
    auto ret = m_localRouterTable->removeNode(_groupID, _nodeID);
    if (ret)
    {
        increaseSeq();
    }
    return ret;
}

void GatewayNodeManager::onReceiveStatusSeq(
    NetworkException const& _e, P2PSession::Ptr _session, std::shared_ptr<P2PMessage> _msg)
{
    if (_e.errorCode())
    {
        NODE_MANAGER_LOG(WARNING) << LOG_DESC("onReceiveStatusSeq error")
                                  << LOG_KV("code", _e.errorCode()) << LOG_KV("msg", _e.what());
        return;
    }
    auto statusSeq = boost::asio::detail::socket_ops::network_to_host_long(
        *((uint32_t*)_msg->payload()->data()));
    auto statusSeqChanged = statusChanged(_session->p2pID(), statusSeq);
    NODE_MANAGER_LOG(DEBUG) << LOG_DESC("onReceiveStatusSeq") << LOG_KV("p2pid", _session->p2pID())
                            << LOG_KV("statusSeq", statusSeq)
                            << LOG_KV("seqChanged", statusSeqChanged);
    if (!statusSeqChanged)
    {
        return;
    }
    m_p2pInterface->sendMessageBySession(MessageType::RequestNodeIDs, bytesConstRef(), _session);
}

bool GatewayNodeManager::statusChanged(std::string const& _p2pNodeID, uint32_t _seq)
{
    bool ret = true;
    ReadGuard l(x_p2pID2Seq);
    auto it = m_p2pID2Seq.find(_p2pNodeID);
    if (it != m_p2pID2Seq.end())
    {
        ret = (statusSeq() != it->second);
    }
    return ret;
}

void GatewayNodeManager::updateNodeIDs(const P2pID& _p2pID, uint32_t _seq,
    const std::map<std::string, std::set<std::string>>& _nodeIDsMap)
{
    NODE_MANAGER_LOG(INFO) << LOG_DESC("updateNodeIDs") << LOG_KV("p2pid", _p2pID)
                           << LOG_KV("statusSeq", _seq);
    {
        UpgradableGuard l(x_p2pID2Seq);
        if (m_p2pID2Seq.count(_p2pID) && (m_p2pID2Seq.at(_p2pID) <= _seq))
        {
            return;
        }
        UpgradeGuard ul(l);
        m_p2pID2Seq[_p2pID] = _seq;
    }
    // remove peers info
    m_peersRouterTable->removeP2PID(_p2pID);
    // insert the latest peers info
    m_peersRouterTable->batchInsertNodeList(_p2pID, _nodeIDsMap);
    m_peersRouterTable->updatePeerNodeList(_p2pID, _nodeIDsMap);
    // notify nodeIDs to front service
    syncLatestNodeIDList();
}


bool GatewayNodeManager::parseReceivedJson(const std::string& _json, uint32_t& statusSeq,
    std::map<std::string, std::set<std::string>>& nodeIDsMap)
{
    try
    {
        Json::Value root;
        Json::Reader jsonReader;
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

void GatewayNodeManager::updateNodeInfo(const P2pID& _p2pID, const std::string& _nodeIDsJson)
{
    // parser info json first
    uint32_t statusSeq;
    std::map<std::string, std::set<std::string>> nodeIDsMap;
    if (parseReceivedJson(_nodeIDsJson, statusSeq, nodeIDsMap))
    {
        updateNodeIDs(_p2pID, statusSeq, nodeIDsMap);
    }
}

void GatewayNodeManager::onResponseNodeIDs(
    NetworkException const& _e, P2PSession::Ptr _session, std::shared_ptr<P2PMessage> _msg)
{
    if (_e.errorCode())
    {
        NODE_MANAGER_LOG(WARNING) << LOG_DESC("onResponseNodeIDs error")
                                  << LOG_KV("code", _e.errorCode()) << LOG_KV("msg", _e.what());
        return;
    }
    updateNodeInfo(
        _session->p2pID(), std::string(_msg->payload()->begin(), _msg->payload()->end()));
}


void GatewayNodeManager::onRequestNodeIDs(
    NetworkException const& _e, P2PSession::Ptr _session, std::shared_ptr<P2PMessage> _msg)
{
    if (_e.errorCode())
    {
        NODE_MANAGER_LOG(WARNING) << LOG_DESC("onRequestNodeIDs network error")
                                  << LOG_KV("code", _e.errorCode()) << LOG_KV("msg", _e.what());
        return;
    }
    std::string nodeInfo;
    if (!generateNodeInfo(nodeInfo))
    {
        NODE_MANAGER_LOG(WARNING) << LOG_DESC("onRequestNodeIDs: generate nodeInfo error")
                                  << LOG_KV("peer", _session->p2pID());
        return;
    }
    if (nodeInfo.size() == 0)
    {
        return;
    }
    m_p2pInterface->sendMessageBySession(MessageType::ResponseNodeIDs,
        bytesConstRef((byte*)nodeInfo.data(), nodeInfo.size()), _session);
}

bool GatewayNodeManager::generateNodeInfo(std::string& _nodeStatusStr)
{
    try
    {
        Json::Value nodeStatus;
        auto seq = statusSeq();
        nodeStatus["statusSeq"] = seq;
        Json::Value nodeListInfo = Json::Value(Json::arrayValue);
        auto nodeList = m_localRouterTable->nodeList();
        for (auto const& it : nodeList)
        {
            Json::Value nodeInfo;
            nodeInfo["groupID"] = it.first;
            nodeInfo["nodeIDs"] = Json::Value(Json::arrayValue);
            for (const auto& info : it.second)
            {
                nodeInfo["nodeIDs"].append(info.first);
            }
            nodeListInfo.append(nodeInfo);
        }
        nodeStatus["nodeInfoList"] = nodeListInfo;
        Json::FastWriter writer;
        _nodeStatusStr = writer.write(nodeStatus);

        NODE_MANAGER_LOG(INFO) << LOG_DESC("generateNodeInfo ") << LOG_KV("seq", seq)
                               << LOG_KV("status", _nodeStatusStr);
        return true;
    }
    catch (const std::exception& e)
    {
        NODE_MANAGER_LOG(ERROR) << LOG_DESC(
            "generateNodeInfo error: " + boost::diagnostic_information(e));
    }
    return false;
}

void GatewayNodeManager::onRemoveNodeIDs(const P2pID& _p2pID)
{
    NODE_MANAGER_LOG(INFO) << LOG_DESC("onRemoveNodeIDs") << LOG_KV("p2pid", _p2pID);
    {
        // remove statusSeq info
        WriteGuard l(x_p2pID2Seq);
        m_p2pID2Seq.erase(_p2pID);
    }
    m_peersRouterTable->removeP2PID(_p2pID);
    // notify nodeIDs to front service
    syncLatestNodeIDList();
}


void GatewayNodeManager::broadcastStatusSeq()
{
    m_timer->restart();
    auto message =
        std::static_pointer_cast<P2PMessage>(m_p2pInterface->messageFactory()->buildMessage());
    message->setPacketType(MessageType::SyncNodeSeq);
    auto seq = statusSeq();
    auto statusSeq = boost::asio::detail::socket_ops::host_to_network_long(seq);
    auto payload = std::make_shared<bytes>((byte*)&statusSeq, (byte*)&statusSeq + 4);
    message->setPayload(payload);
    NODE_MANAGER_LOG(DEBUG) << LOG_DESC("broadcastStatusSeq") << LOG_KV("seq", seq);
    m_p2pInterface->asyncBroadcastMessage(message, Options());
}

void GatewayNodeManager::syncLatestNodeIDList()
{
    auto nodeList = m_localRouterTable->nodeList();
    for (auto const& it : nodeList)
    {
        auto groupID = it.first;
        auto const& groupNodeInfos = it.second;
        auto knowNodeIDs = getGroupNodeIDList(groupID);
        NODE_MANAGER_LOG(INFO) << LOG_DESC("syncLatestNodeIDList") << LOG_KV("groupID", groupID)
                               << LOG_KV("nodeCount", knowNodeIDs->size());
        for (const auto& entry : groupNodeInfos)
        {
            entry.second->frontService()->onReceiveNodeIDs(
                groupID, knowNodeIDs, [](Error::Ptr _error) {
                    if (!_error)
                    {
                        return;
                    }
                    NODE_MANAGER_LOG(WARNING)
                        << LOG_DESC("syncLatestNodeIDList onReceiveNodeIDs callback")
                        << LOG_KV("codeCode", _error->errorCode())
                        << LOG_KV("codeMessage", _error->errorMessage());
                });
        }
    }
}

NodeIDListPtr GatewayNodeManager::getGroupNodeIDList(const std::string& _groupID)
{
    auto nodeIDList = std::make_shared<NodeIDs>();
    auto localNodeIDList = m_localRouterTable->getGroupNodeIDList(_groupID);
    *nodeIDList = std::move(localNodeIDList);

    auto peersNodeIDList = m_peersRouterTable->getGroupNodeIDList(_groupID);
    nodeIDList->insert(nodeIDList->begin(), peersNodeIDList.begin(), peersNodeIDList.end());
    return nodeIDList;
}

std::map<std::string, std::set<std::string>> GatewayNodeManager::peersNodeInfo(
    std::string const& _p2pNodeID)
{
    if (_p2pNodeID == m_p2pNodeID)
    {
        return m_localRouterTable->nodeListInfo();
    }
    return m_peersRouterTable->peersNodeInfo(_p2pNodeID);
}