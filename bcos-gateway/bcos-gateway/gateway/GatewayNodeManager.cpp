
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
#include <bcos-utilities/DataConvertUtility.h>

using namespace std;
using namespace bcos;
using namespace gateway;
using namespace bcos::protocol;
using namespace bcos::group;
using namespace bcos::crypto;

GatewayNodeManager::GatewayNodeManager(std::string const& _uuid, P2pID const& _nodeID,
    std::shared_ptr<bcos::crypto::KeyFactory> _keyFactory, P2PInterface::Ptr _p2pInterface)
  : GatewayNodeManager(_uuid, _keyFactory, _p2pInterface)
{
    m_uuid = _uuid;
    if (_uuid.size() == 0)
    {
        m_uuid = _nodeID;
    }
    m_p2pNodeID = _nodeID;
    m_p2pInterface = _p2pInterface;
    // SyncNodeSeq
    m_p2pInterface->registerHandlerByMsgType(MessageType::SyncNodeSeq,
        boost::bind(&GatewayNodeManager::onReceiveStatusSeq, this, boost::placeholders::_1,
            boost::placeholders::_2, boost::placeholders::_3));
    // RequestNodeStatus
    m_p2pInterface->registerHandlerByMsgType(MessageType::RequestNodeStatus,
        boost::bind(&GatewayNodeManager::onRequestNodeStatus, this, boost::placeholders::_1,
            boost::placeholders::_2, boost::placeholders::_3));
    // ResponseNodeStatus
    m_p2pInterface->registerHandlerByMsgType(MessageType::ResponseNodeStatus,
        boost::bind(&GatewayNodeManager::onReceiveNodeStatus, this, boost::placeholders::_1,
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
        m_p2pInterface->eraseHandlerByMsgType(MessageType::RequestNodeStatus);
        m_p2pInterface->eraseHandlerByMsgType(MessageType::ResponseNodeStatus);
    }
    if (m_timer)
    {
        m_timer->stop();
    }
}

bool GatewayNodeManager::registerNode(const std::string& _groupID, bcos::crypto::NodeIDPtr _nodeID,
    bcos::protocol::NodeType _nodeType, bcos::front::FrontServiceInterface::Ptr _frontService)
{
    auto ret = m_localRouterTable->insertNode(_groupID, _nodeID, _nodeType, _frontService);
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
    NODE_MANAGER_LOG(TRACE) << LOG_DESC("onReceiveStatusSeq") << LOG_KV("p2pid", _session->p2pID())
                            << LOG_KV("statusSeq", statusSeq)
                            << LOG_KV("seqChanged", statusSeqChanged);
    if (!statusSeqChanged)
    {
        return;
    }
    m_p2pInterface->sendMessageBySession(MessageType::RequestNodeStatus, bytesConstRef(), _session);
}

bool GatewayNodeManager::statusChanged(std::string const& _p2pNodeID, uint32_t _seq)
{
    bool ret = true;
    ReadGuard l(x_p2pID2Seq);
    auto it = m_p2pID2Seq.find(_p2pNodeID);
    if (it != m_p2pID2Seq.end())
    {
        ret = (_seq > it->second);
    }
    return ret;
}

void GatewayNodeManager::onReceiveNodeStatus(
    NetworkException const& _e, P2PSession::Ptr _session, std::shared_ptr<P2PMessage> _msg)
{
    if (_e.errorCode())
    {
        NODE_MANAGER_LOG(WARNING) << LOG_DESC("onReceiveNodeStatus error")
                                  << LOG_KV("code", _e.errorCode()) << LOG_KV("msg", _e.what());
        return;
    }
    auto gatewayNodeStatus = m_gatewayNodeStatusFactory->createGatewayNodeStatus();
    gatewayNodeStatus->decode(bytesConstRef(_msg->payload()->data(), _msg->payload()->size()));
    auto p2pID = _session->p2pID();
    NODE_MANAGER_LOG(INFO) << LOG_DESC("onReceiveNodeStatus") << LOG_KV("p2pid", p2pID)
                           << LOG_KV("seq", gatewayNodeStatus->seq())
                           << LOG_KV("uuid", gatewayNodeStatus->uuid());
    updatePeerStatus(p2pID, gatewayNodeStatus);
}

void GatewayNodeManager::updatePeerStatus(std::string const& _p2pID, GatewayNodeStatus::Ptr _status)
{
    auto seq = _status->seq();
    {
        UpgradableGuard l(x_p2pID2Seq);
        if (m_p2pID2Seq.count(_p2pID) && (m_p2pID2Seq.at(_p2pID) >= seq))
        {
            return;
        }
        UpgradeGuard ul(l);
        m_p2pID2Seq[_p2pID] = seq;
    }
    // remove peers info
    // insert the latest peers info
    m_peersRouterTable->updatePeerStatus(_p2pID, _status);
    // notify nodeIDs to front service
    syncLatestNodeIDList();
}

void GatewayNodeManager::onRequestNodeStatus(
    NetworkException const& _e, P2PSession::Ptr _session, std::shared_ptr<P2PMessage> _msg)
{
    if (_e.errorCode())
    {
        NODE_MANAGER_LOG(WARNING) << LOG_DESC("onRequestNodeStatus network error")
                                  << LOG_KV("code", _e.errorCode()) << LOG_KV("msg", _e.what());
        return;
    }
    auto nodeStatusData = generateNodeStatus();
    if (!nodeStatusData)
    {
        NODE_MANAGER_LOG(WARNING) << LOG_DESC("onRequestNodeStatus: generate nodeInfo error")
                                  << LOG_KV("peer", _session->p2pID());
        return;
    }
    m_p2pInterface->sendMessageBySession(MessageType::ResponseNodeStatus,
        bytesConstRef((byte*)nodeStatusData->data(), nodeStatusData->size()), _session);
}

bytesPointer GatewayNodeManager::generateNodeStatus()
{
    auto nodeStatus = m_gatewayNodeStatusFactory->createGatewayNodeStatus();
    nodeStatus->setUUID(m_uuid);
    nodeStatus->setSeq(statusSeq());
    auto nodeList = m_localRouterTable->nodeList();
    std::vector<GroupNodeInfo::Ptr> groupNodeInfos;
    for (auto const& it : nodeList)
    {
        auto groupNodeInfo = m_gatewayNodeStatusFactory->createGroupNodeInfo();
        groupNodeInfo->setGroupID(it.first);
        // get nodeID and type
        std::vector<std::string> nodeIDList;
        auto groupType = GroupType::OUTSIDE_GROUP;
        bool hasObserverNode = false;
        for (auto const& pNodeInfo : it.second)
        {
            nodeIDList.emplace_back(pNodeInfo.first);
            if ((NodeType)(pNodeInfo.second->nodeType()) == NodeType::CONSENSUS_NODE)
            {
                groupType = GroupType::GROUP_WITH_CONSENSUS_NODE;
            }
            if ((NodeType)(pNodeInfo.second->nodeType()) == NodeType::OBSERVER_NODE)
            {
                hasObserverNode = true;
            }
        }
        if (groupType == GroupType::OUTSIDE_GROUP && hasObserverNode)
        {
            groupType = GroupType::GROUP_WITHOUT_CONSENSUS_NODE;
        }
        groupNodeInfo->setType(groupType);
        groupNodeInfo->setNodeIDList(std::move(nodeIDList));
        groupNodeInfos.emplace_back(groupNodeInfo);
        NODE_MANAGER_LOG(INFO) << LOG_DESC("generateNodeStatus") << LOG_KV("groupType", groupType)
                               << LOG_KV("groupID", it.first)
                               << LOG_KV("nodeListSize", groupNodeInfo->nodeIDList().size())
                               << LOG_KV("seq", statusSeq());
    }
    nodeStatus->setGroupNodeInfos(std::move(groupNodeInfos));
    return nodeStatus->encode();
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
    NODE_MANAGER_LOG(TRACE) << LOG_DESC("broadcastStatusSeq") << LOG_KV("seq", seq);
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

std::map<std::string, std::set<std::string>> GatewayNodeManager::peersNodeIDList(
    std::string const& _p2pNodeID)
{
    if (_p2pNodeID == m_p2pNodeID)
    {
        return m_localRouterTable->nodeListInfo();
    }
    return m_peersRouterTable->peersNodeIDList(_p2pNodeID);
}