
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
#include <bcos-gateway/libp2p/P2PMessageV2.h>
#include <bcos-utilities/DataConvertUtility.h>

using namespace std;
using namespace bcos;
using namespace gateway;
using namespace bcos::protocol;
using namespace bcos::group;
using namespace bcos::crypto;
using namespace bcos::boostssl::ws;

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
    m_p2pInterface->registerHandlerByMsgType(
        GatewayMessageType::SyncNodeSeq, boost::bind(&GatewayNodeManager::onReceiveStatusSeq, this,
                                             boost::placeholders::_1, boost::placeholders::_2));
    // RequestNodeStatus
    m_p2pInterface->registerHandlerByMsgType(GatewayMessageType::RequestNodeStatus,
        boost::bind(&GatewayNodeManager::onRequestNodeStatus, this, boost::placeholders::_1,
            boost::placeholders::_2));
    // ResponseNodeStatus
    m_p2pInterface->registerHandlerByMsgType(GatewayMessageType::ResponseNodeStatus,
        boost::bind(&GatewayNodeManager::onReceiveNodeStatus, this, boost::placeholders::_1,
            boost::placeholders::_2));
    m_timer = std::make_shared<Timer>(SEQ_SYNC_PERIOD, "seqSync");
    // broadcast seq periodically
    m_timer->registerTimeoutHandler([this]() { broadcastStatusSeq(); });
}

void GatewayNodeManager::stop()
{
    if (m_p2pInterface)
    {
        m_p2pInterface->eraseHandlerByMsgType(GatewayMessageType::SyncNodeSeq);
        m_p2pInterface->eraseHandlerByMsgType(GatewayMessageType::RequestNodeStatus);
        m_p2pInterface->eraseHandlerByMsgType(GatewayMessageType::ResponseNodeStatus);
    }
    if (m_timer)
    {
        m_timer->stop();
    }
}

bool GatewayNodeManager::registerNode(const std::string& _groupID, bcos::crypto::NodeIDPtr _nodeID,
    bcos::protocol::NodeType _nodeType, bcos::front::FrontServiceInterface::Ptr _frontService,
    bcos::protocol::ProtocolInfo::ConstPtr _protocolInfo)
{
    auto ret =
        m_localRouterTable->insertNode(_groupID, _nodeID, _nodeType, _frontService, _protocolInfo);
    if (ret)
    {
        increaseSeq();
    }
    return ret;
}

bool GatewayNodeManager::unregisterNode(const std::string& _groupID, std::string const& _nodeID)
{
    auto ret = m_localRouterTable->removeNode(_groupID, _nodeID);
    if (ret)
    {
        increaseSeq();
    }
    return ret;
}

void GatewayNodeManager::onReceiveStatusSeq(
    std::shared_ptr<P2PSession> _p2pSession, std::shared_ptr<P2PMessage> _msg)
{
    auto statusSeq = boost::asio::detail::socket_ops::network_to_host_long(
        *((uint32_t*)_msg->payload()->data()));
    auto const& from =
        (_msg->srcP2PNodeID().size() > 0) ? _msg->srcP2PNodeID() : _p2pSession->nodeId();
    auto statusSeqChanged = statusChanged(from, statusSeq);
    if (!statusSeqChanged)
    {
        return;
    }
    NODE_MANAGER_LOG(TRACE) << LOG_DESC("onReceiveStatusSeq request nodeStatus")
                            << LOG_KV("from", from) << LOG_KV("statusSeq", statusSeq);
    m_p2pInterface->asyncSendMessageByP2PNodeID(
        GatewayMessageType::RequestNodeStatus, from, bytesConstRef());
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
    std::shared_ptr<P2PSession> _p2pSession, std::shared_ptr<P2PMessage> _msg)
{
    auto gatewayNodeStatus = m_gatewayNodeStatusFactory->createGatewayNodeStatus();
    gatewayNodeStatus->decode(bytesConstRef(_msg->payload()->data(), _msg->payload()->size()));
    auto const& from =
        (_msg->srcP2PNodeID().size() > 0) ? _msg->srcP2PNodeID() : _p2pSession->nodeId();

    NODE_MANAGER_LOG(INFO) << LOG_DESC("onReceiveNodeStatus") << LOG_KV("from", from)
                           << LOG_KV("seq", gatewayNodeStatus->seq())
                           << LOG_KV("uuid", gatewayNodeStatus->uuid());
    updatePeerStatus(from, gatewayNodeStatus);
}

void GatewayNodeManager::updatePeerStatus(std::string const& _p2pID, GatewayNodeStatus::Ptr _status)
{
    auto seq = boost::lexical_cast<uint32_t>(_status->seq());
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

bool GatewayNodeManager::updateFrontServiceInfo(bcos::group::GroupInfo::Ptr _groupInfo)
{
    auto updated = m_localRouterTable->updateGroupNodeInfos(_groupInfo);
    if (updated)
    {
        increaseSeq();
        syncLatestNodeIDList();
    }
    return updated;
}

void GatewayNodeManager::onRequestNodeStatus(
    std::shared_ptr<P2PSession> _p2pSession, std::shared_ptr<P2PMessage> _msg)
{
    auto const& from =
        (_msg->srcP2PNodeID().size() > 0) ? _msg->srcP2PNodeID() : _p2pSession->nodeId();
    auto nodeStatusData = generateNodeStatus();
    if (!nodeStatusData)
    {
        NODE_MANAGER_LOG(WARNING) << LOG_DESC("onRequestNodeStatus: generate nodeInfo error")
                                  << LOG_KV("from", from);
        return;
    }
    NODE_MANAGER_LOG(TRACE) << LOG_DESC("onRequestNodeStatus") << LOG_KV("from", from);
    m_p2pInterface->asyncSendMessageByP2PNodeID(GatewayMessageType::ResponseNodeStatus, from,
        bytesConstRef((byte*)nodeStatusData->data(), nodeStatusData->size()));
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
        std::vector<ProtocolInfo::ConstPtr> protocolList;

        auto groupType = GroupType::OUTSIDE_GROUP;
        bool hasObserverNode = false;
        for (auto const& pNodeInfo : it.second)
        {
            nodeIDList.emplace_back(pNodeInfo.first);
            protocolList.emplace_back(pNodeInfo.second->protocolInfo());
            if ((NodeType)(pNodeInfo.second->nodeType()) == NodeType::CONSENSUS_NODE)
            {
                groupType = GroupType::GROUP_WITH_CONSENSUS_NODE;
            }
            // the group has observerNode
            if ((NodeType)(pNodeInfo.second->nodeType()) == NodeType::OBSERVER_NODE)
            {
                hasObserverNode = true;
            }
        }
        // the group has only observerNode
        if (groupType == GroupType::OUTSIDE_GROUP && hasObserverNode)
        {
            groupType = GroupType::GROUP_WITHOUT_CONSENSUS_NODE;
        }
        groupNodeInfo->setType(groupType);
        groupNodeInfo->setNodeIDList(std::move(nodeIDList));
        groupNodeInfo->setNodeProtocolList(std::move(protocolList));
        groupNodeInfos.emplace_back(groupNodeInfo);
        NODE_MANAGER_LOG(INFO) << LOG_DESC("generateNodeStatus") << LOG_KV("groupType", groupType)
                               << LOG_KV("groupID", it.first)
                               << LOG_KV("nodeListSize", groupNodeInfo->nodeIDList().size())
                               << LOG_KV("seq", statusSeq());
    }
    NODE_MANAGER_LOG(INFO) << LOG_DESC("generateNodeStatus")
                           << LOG_KV("nodeListSize", nodeList.size());
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
    message->setPacketType(GatewayMessageType::SyncNodeSeq);
    auto seq = statusSeq();
    auto statusSeq = boost::asio::detail::socket_ops::host_to_network_long(seq);
    auto payload = std::make_shared<bytes>((byte*)&statusSeq, (byte*)&statusSeq + 4);
    message->setPayload(payload);
    NODE_MANAGER_LOG(TRACE) << LOG_DESC("broadcastStatusSeq") << LOG_KV("seq", seq);
    m_p2pInterface->asyncBroadcastMessage(message, boostssl::ws::Options());
}

void GatewayNodeManager::syncLatestNodeIDList()
{
    auto nodeList = m_localRouterTable->nodeList();
    for (auto const& it : nodeList)
    {
        auto groupID = it.first;
        auto const& localNodeEntryPoints = it.second;
        auto groupNodeInfos = getGroupNodeInfoList(groupID);
        NODE_MANAGER_LOG(INFO) << LOG_DESC("syncLatestNodeIDList") << LOG_KV("groupID", groupID)
                               << LOG_KV("nodeCount", groupNodeInfos->nodeIDList().size());
        for (const auto& entry : localNodeEntryPoints)
        {
            entry.second->frontService()->onReceiveGroupNodeInfo(
                groupID, groupNodeInfos, [](Error::Ptr _error) {
                    if (!_error)
                    {
                        return;
                    }
                    NODE_MANAGER_LOG(WARNING)
                        << LOG_DESC("syncLatestNodeIDList onReceiveGroupNodeInfo callback")
                        << LOG_KV("codeCode", _error->errorCode())
                        << LOG_KV("codeMessage", _error->errorMessage());
                });
        }
    }
}

GroupNodeInfo::Ptr GatewayNodeManager::getGroupNodeInfoList(const std::string& _groupID)
{
    auto groupNodeInfo = m_gatewayNodeStatusFactory->createGroupNodeInfo();
    groupNodeInfo->setGroupID(_groupID);

    m_localRouterTable->getGroupNodeInfoList(groupNodeInfo, _groupID);
    m_peersRouterTable->getGroupNodeInfoList(groupNodeInfo, _groupID);
    return groupNodeInfo;
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