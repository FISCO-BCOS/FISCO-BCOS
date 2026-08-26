
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
#include "bcos-gateway/libp2p/P2PMessageV2.h"
#include <bcos-task/Wait.h>
#include <cstring>

using namespace std;
using namespace bcos;
using namespace gateway;
using namespace bcos::protocol;
using namespace bcos::group;
using namespace bcos::crypto;

GatewayNodeManager::~GatewayNodeManager() = default;

void GatewayNodeManager::start()
{
    m_timer->start();
}

LocalRouterTable::Ptr GatewayNodeManager::localRouterTable()
{
    return m_localRouterTable;
}

PeersRouterTable::Ptr GatewayNodeManager::peersRouterTable()
{
    return m_peersRouterTable;
}

std::shared_ptr<bcos::crypto::KeyFactory> GatewayNodeManager::keyFactory()
{
    return m_keyFactory;
}

GatewayNodeManager::GatewayNodeManager(std::string const& _uuid,
    std::shared_ptr<bcos::crypto::KeyFactory> _keyFactory, P2PInterface::Ptr _p2pInterface)
  : m_uuid(_uuid),
    m_keyFactory(_keyFactory),
    m_localRouterTable(std::make_shared<LocalRouterTable>(_keyFactory)),
    m_peersRouterTable(std::make_shared<PeersRouterTable>(_uuid, _keyFactory, _p2pInterface)),
    m_gatewayNodeStatusFactory(std::make_shared<GatewayNodeStatusFactory>())
{}

uint32_t GatewayNodeManager::increaseSeq()
{
    uint32_t statusSeq = ++m_statusSeq;
    return statusSeq;
}

uint32_t GatewayNodeManager::statusSeq()
{
    return m_statusSeq;
}

GatewayNodeManager::GatewayNodeManager(std::string const& _uuid, P2pID const& _nodeID,
    std::shared_ptr<bcos::crypto::KeyFactory> _keyFactory, P2PInterface::Ptr _p2pInterface,
    boost::asio::io_context& _ioContext)
  : GatewayNodeManager(_uuid, _keyFactory, _p2pInterface)
{
    m_uuid = _uuid;
    if (_uuid.empty())
    {
        m_uuid = _nodeID;
    }
    m_p2pNodeID = _nodeID;
    m_p2pInterface = _p2pInterface;
    // SyncNodeSeq
    m_p2pInterface->registerHandlerByMsgType(GatewayMessageType::SyncNodeSeq,
        [this](NetworkException const& _e, P2PSession::Ptr _session,
            std::shared_ptr<P2PMessage> _msg) {
            onReceiveStatusSeq(_e, _session, std::move(_msg));
        });
    // RequestNodeStatus
    m_p2pInterface->registerHandlerByMsgType(GatewayMessageType::RequestNodeStatus,
        [this](NetworkException const& _e, P2PSession::Ptr _session,
            std::shared_ptr<P2PMessage> _msg) {
            onRequestNodeStatus(_e, _session, std::move(_msg));
        });
    // ResponseNodeStatus
    m_p2pInterface->registerHandlerByMsgType(GatewayMessageType::ResponseNodeStatus,
        [this](NetworkException const& _e, P2PSession::Ptr _session,
            std::shared_ptr<P2PMessage> _msg) {
            onReceiveNodeStatus(_e, _session, std::move(_msg));
        });
    m_timer = std::make_shared<Timer>(_ioContext, SEQ_SYNC_PERIOD, "seqSync");
    // broadcast seq periodically; also flush a coalesced node-list sync if peers dropped since the
    // last tick (FIB-186 vector D: one sync per period instead of one per dropped session)
    m_timer->registerTimeoutHandler([this]() {
        if (m_nodeIDListDirty.exchange(false, std::memory_order_acq_rel))
        {
            // FIB-186 (vector D): syncLatestNodeIDList runs on the timer's io_context, before
            // broadcastStatusSeq() re-arms the timer (m_timer->restart() is its first line).
            // Timer's async_wait handler only logs a throwing timeout handler and does NOT
            // reschedule, so an exception escaping here would leave the timer un-rearmed and the
            // seqSync timer would die permanently (all status-seq broadcasts and node-list syncs
            // would stop). Contain it so broadcastStatusSeq() always runs and re-arms the timer.
            try
            {
                syncLatestNodeIDList();
            }
            catch (std::exception const& e)
            {
                NODE_MANAGER_LOG(WARNING)
                    << LOG_DESC("syncLatestNodeIDList exception") << LOG_KV("error", e.what());
            }
        }
        broadcastStatusSeq();
    });
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
    NetworkException const& _e, P2PSession::Ptr _session, std::shared_ptr<P2PMessage> _msg)
{
    if (_e.errorCode())
    {
        NODE_MANAGER_LOG(WARNING) << LOG_DESC("onReceiveStatusSeq error")
                                  << LOG_KV("code", _e.errorCode()) << LOG_KV("msg", _e.what());
        return;
    }
    // FIB-183: onReceiveStatusSeq reads a 4-byte sequence via *(uint32_t*)payload().data()
    // with no size check (same defect class fixed in ServiceV2::onReceiveRouterSeq); a 0-3 byte
    // payload reads past the decoded buffer. Drop short payloads and use memcpy for the read.
    if (_msg->payload().size() < sizeof(uint32_t))
    {
        NODE_MANAGER_LOG(WARNING) << LOG_DESC("onReceiveStatusSeq short payload, drop")
                                  << LOG_KV("size", _msg->payload().size());
        return;
    }
    uint32_t rawStatusSeq = 0;
    std::memcpy(&rawStatusSeq, _msg->payload().data(), sizeof(rawStatusSeq));
    auto statusSeq = boost::asio::detail::socket_ops::network_to_host_long(rawStatusSeq);
    auto const& from = (_msg->srcP2PNodeID().size() > 0) ? _msg->srcP2PNodeID() : _session->p2pID();
    auto statusSeqChanged = statusChanged(from, statusSeq);
    if (!statusSeqChanged)
    {
        return;
    }
    NODE_MANAGER_LOG(TRACE) << LOG_DESC("onReceiveStatusSeq request nodeStatus")
                            << LOG_KV("from", printShortP2pID(from))
                            << LOG_KV("statusSeq", statusSeq);
    m_p2pInterface->asyncSendMessageByP2PNodeID(
        GatewayMessageType::RequestNodeStatus, from, bytesConstRef());
}

bool GatewayNodeManager::statusChanged(std::string const& _p2pNodeID, uint32_t _seq)
{
    if (decltype(m_p2pID2Seq)::const_accessor accessor; m_p2pID2Seq.find(accessor, _p2pNodeID))
    {
        return (_seq > accessor->second);
    }
    return true;
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
    gatewayNodeStatus->decode(bytesConstRef(_msg->payload().data(), _msg->payload().size()));
    auto const& from = (!_msg->srcP2PNodeID().empty()) ? _msg->srcP2PNodeID() : _session->p2pID();

    NODE_MANAGER_LOG(INFO) << LOG_DESC("onReceiveNodeStatus")
                           << LOG_KV("from", printShortP2pID(from))
                           << LOG_KV("seq", gatewayNodeStatus->seq())
                           << LOG_KV("uuid", gatewayNodeStatus->uuid());
    updatePeerStatus(from, gatewayNodeStatus);
}

void GatewayNodeManager::updatePeerStatus(std::string const& _p2pID, GatewayNodeStatus::Ptr _status)
{
    auto seq = _status->seq();
    decltype(m_p2pID2Seq)::accessor accessor;
    if (!m_p2pID2Seq.insert(accessor, _p2pID) && (accessor->second >= seq))
    {
        return;
    }
    accessor->second = seq;
    accessor.release();
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
    NetworkException const& _e, P2PSession::Ptr _session, std::shared_ptr<P2PMessage> _msg)
{
    if (_e.errorCode())
    {
        NODE_MANAGER_LOG(WARNING) << LOG_DESC("onRequestNodeStatus network error")
                                  << LOG_KV("code", _e.errorCode()) << LOG_KV("msg", _e.what());
        return;
    }
    auto const& from = (!_msg->srcP2PNodeID().empty()) ? _msg->srcP2PNodeID() : _session->p2pID();
    auto nodeStatusData = generateNodeStatus();
    if (!nodeStatusData)
    {
        NODE_MANAGER_LOG(WARNING) << LOG_DESC("onRequestNodeStatus: generate nodeInfo error")
                                  << LOG_KV("from", printShortP2pID(from));
        return;
    }
    NODE_MANAGER_LOG(TRACE) << LOG_DESC("onRequestNodeStatus")
                            << LOG_KV("from", printShortP2pID(from));
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
        std::vector<int32_t> nodeTypeList;
        std::vector<ProtocolInfo::ConstPtr> protocolList;

        auto groupType = GroupType::OUTSIDE_GROUP;
        bool hasObserverNode = false;
        for (auto const& pNodeInfo : it.second)
        {
            nodeIDList.emplace_back(pNodeInfo.first);
            nodeTypeList.emplace_back(pNodeInfo.second->nodeType());
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
        groupNodeInfo->setNodeTypeList(std::move(nodeTypeList));
        groupNodeInfo->setNodeProtocolList(std::move(protocolList));
        groupNodeInfos.emplace_back(groupNodeInfo);
        NODE_MANAGER_LOG(INFO) << LOG_DESC("generateNodeStatus") << LOG_KV("groupType", groupType)
                               << LOG_KV("groupID", it.first)
                               << LOG_KV("nodeListSize", groupNodeInfo->nodeIDList().size())
                               << LOG_KV("nodeTypeListSize", groupNodeInfo->nodeTypeList().size())
                               << LOG_KV("seq", statusSeq());
    }
    NODE_MANAGER_LOG(INFO) << LOG_DESC("generateNodeStatus")
                           << LOG_KV("nodeListSize", nodeList.size());
    nodeStatus->setGroupNodeInfos(std::move(groupNodeInfos));
    return nodeStatus->encode();
}

void GatewayNodeManager::onRemoveNodeIDs(const P2pID& _p2pID)
{
    NODE_MANAGER_LOG(INFO) << LOG_DESC("onRemoveNodeIDs")
                           << LOG_KV("p2pid", printShortP2pID(_p2pID));
    {
        // remove statusSeq info
        m_p2pID2Seq.erase(_p2pID);
    }
    m_peersRouterTable->removeP2PID(_p2pID);
    // FIB-186 (vector D): notify the local front promptly without re-introducing the per-disconnect
    // sync flood. removeP2PID above updates the gateway's own routing table immediately; the
    // front's cached node-list is refreshed by syncLatestNodeIDList. Sync inline only on the first
    // drop of a burst (the clean->dirty transition); further drops within the window leave the flag
    // dirty and are coalesced by the seqSync timer (at most one inline sync per SEQ_SYNC_PERIOD, so
    // a persistent bulk-disconnect cannot flood the front). Without the inline sync the front would
    // keep a stale node-list for up to a full period, issuing messages to the departed node that
    // the gateway then drops. This runs on the teardown executor inside Service::onMessage's
    // try/catch, but wrap it so a throw cannot skip the rest of onDisconnect (other disconnect
    // handlers, session erase).
    if (!m_nodeIDListDirty.exchange(true, std::memory_order_acq_rel))
    {
        try
        {
            syncLatestNodeIDList();
        }
        catch (std::exception const& e)
        {
            NODE_MANAGER_LOG(WARNING)
                << LOG_DESC("onRemoveNodeIDs inline sync exception") << LOG_KV("error", e.what());
        }
    }
}


void GatewayNodeManager::broadcastStatusSeq()
{
    m_timer->restart();
    auto seq = statusSeq();
    auto statusSeq = boost::asio::detail::socket_ops::host_to_network_long(seq);
    bytes payload;
    payload.insert(payload.end(), (byte*)&statusSeq, (byte*)&statusSeq + 4);
    NODE_MANAGER_LOG(TRACE) << LOG_DESC("broadcastStatusSeq") << LOG_KV("seq", seq);
    auto p2p = m_p2pInterface;
    // value message held by shared_ptr; the 4-byte seq payload is owned by it (zero-copy view
    // send). The p2p interface is passed as a coroutine parameter so it is copied into the frame
    // and stays alive for the whole (possibly deferred) send.
    task::wait([](P2PInterface::Ptr _p2p, bcos::bytes _payload) mutable
                   -> task::Task<void> {
        auto message = std::make_shared<P2PMessageV2>();
        message->setPacketType(GatewayMessageType::SyncNodeSeq);
        message->setPayload(std::move(_payload));
        co_await _p2p->broadcastMessageToAll(
            message, ::ranges::views::single(message->payload()), Options{});
    }(p2p, std::move(payload)));
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

std::map<std::string, std::map<std::string, uint32_t>> GatewayNodeManager::peersNodeIDList(
    std::string const& _p2pNodeID)
{
    if (_p2pNodeID == m_p2pNodeID)
    {
        return m_localRouterTable->nodeListInfo();
    }
    return m_peersRouterTable->peersNodeIDList(_p2pNodeID);
}
