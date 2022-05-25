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
 * @file ServiceV2.cpp
 * @author: yujiechen
 * @date 2022-5-24
 */
#include "ServiceV2.h"
#include "Common.h"
#include "P2PMessageV2.h"

using namespace bcos;
using namespace bcos::gateway;

ServiceV2::ServiceV2(std::string const& _nodeID, RouterTableFactory::Ptr _routerTableFactory)
  : Service(_nodeID),
    m_routerTableFactory(_routerTableFactory),
    m_routerTable(m_routerTableFactory->createRouterTable())
{
    m_routerTable->setNodeID(m_nodeID);
    m_routerTable->setUnreachableTTL(c_unreachableTTL);
    // process router packet related logic
    registerHandlerByMsgType(GatewayMessageType::RouterTableSyncSeq,
        boost::bind(&ServiceV2::onReceiveRouterSeq, this, boost::placeholders::_1,
            boost::placeholders::_2, boost::placeholders::_3));

    registerHandlerByMsgType(GatewayMessageType::RouterTableResponse,
        boost::bind(&ServiceV2::onReceivePeersRouterTable, this, boost::placeholders::_1,
            boost::placeholders::_2, boost::placeholders::_3));

    registerHandlerByMsgType(GatewayMessageType::RouterTableRequest,
        boost::bind(&ServiceV2::onReceiveRouterTableRequest, this, boost::placeholders::_1,
            boost::placeholders::_2, boost::placeholders::_3));
    registerOnNewSession([this](P2PSession::Ptr _session) { onNewSession(_session); });
    registerOnDeleteSession([this](P2PSession::Ptr _session) { onEraseSession(_session); });
    m_routerTimer = std::make_shared<bcos::Timer>(3000, "routerSeqSync");
    m_routerTimer->registerTimeoutHandler([this]() { broadcastRouterSeq(); });
}

void ServiceV2::start()
{
    Service::start();
    if (m_routerTimer)
    {
        m_routerTimer->start();
    }
}

void ServiceV2::stop()
{
    if (m_routerTimer)
    {
        m_routerTimer->stop();
    }
    Service::stop();
}

// receive routerTable from peers
void ServiceV2::onReceivePeersRouterTable(
    NetworkException _e, std::shared_ptr<P2PSession> _session, P2PMessage::Ptr _message)
{
    if (_e.errorCode())
    {
        SERVICE_ROUTER_LOG(WARNING) << LOG_DESC("onReceivePeersRouterTable error")
                                    << LOG_KV("code", _e.errorCode()) << LOG_KV("msg", _e.what());
        return;
    }
    auto routerTable = m_routerTableFactory->createRouterTable(ref(*(_message->payload())));

    SERVICE_ROUTER_LOG(INFO) << LOG_DESC("onReceivePeersRouterTable")
                             << LOG_KV("peer", _session->p2pID())
                             << LOG_KV("entrySize", routerTable->routerEntries().size());
    joinRouterTable(_session->p2pID(), routerTable);
}

void ServiceV2::joinRouterTable(
    std::string const& _generatedFrom, RouterTableInterface::Ptr _routerTable)
{
    std::set<std::string> unreachableNodes;
    bool updated = false;
    auto const& entries = _routerTable->routerEntries();
    for (auto const& it : entries)
    {
        auto entry = it.second;
        if (m_routerTable->update(unreachableNodes, _generatedFrom, entry) && !updated)
        {
            updated = true;
        }
    }
    SERVICE_ROUTER_LOG(INFO) << LOG_DESC("joinRouterTable: create router entry")
                             << LOG_KV("dst", _generatedFrom);
    auto entry = m_routerTableFactory->createRouterEntry();
    entry->setDstNode(_generatedFrom);
    entry->setTTL(0);
    if (m_routerTable->update(unreachableNodes, m_nodeID, entry) && !updated)
    {
        updated = true;
    }
    if (!updated)
    {
        return;
    }
    onP2PNodesUnreachable(unreachableNodes);
    m_statusSeq++;
    broadcastRouterSeq();
}

// receive routerTable request from peer
void ServiceV2::onReceiveRouterTableRequest(
    NetworkException _e, std::shared_ptr<P2PSession> _session, P2PMessage::Ptr _message)
{
    if (_e.errorCode())
    {
        SERVICE_ROUTER_LOG(WARNING) << LOG_DESC("onReceiveRouterTableRequest error")
                                    << LOG_KV("code", _e.errorCode()) << LOG_KV("msg", _e.what());
        return;
    }
    SERVICE_ROUTER_LOG(INFO) << LOG_DESC("onReceiveRouterTableRequest and response routerTable")
                             << LOG_KV("peer", _session->p2pID())
                             << LOG_KV("entrySize", m_routerTable->routerEntries().size());

    auto routerTableData = std::make_shared<bytes>();
    m_routerTable->encode(*routerTableData);
    auto dstP2PNodeID =
        (_message->srcP2PNodeID().size() > 0) ? _message->srcP2PNodeID() : _session->p2pID();
    asyncSendMessageByP2PNodeID(GatewayMessageType::RouterTableResponse, dstP2PNodeID,
        bytesConstRef((byte*)routerTableData->data(), routerTableData->size()));
}

void ServiceV2::broadcastRouterSeq()
{
    m_routerTimer->restart();
    auto message = std::static_pointer_cast<P2PMessage>(m_messageFactory->buildMessage());
    message->setPacketType(GatewayMessageType::RouterTableSyncSeq);
    auto seq = m_statusSeq.load();
    auto statusSeq = boost::asio::detail::socket_ops::host_to_network_long(seq);
    auto payload = std::make_shared<bytes>((byte*)&statusSeq, (byte*)&statusSeq + 4);
    message->setPayload(payload);
    // the router table should only exchange between neighbor
    asyncBroadcastMessageWithoutForward(message, Options());
}

void ServiceV2::onReceiveRouterSeq(
    NetworkException _e, std::shared_ptr<P2PSession> _session, P2PMessage::Ptr _message)
{
    if (_e.errorCode())
    {
        SERVICE_ROUTER_LOG(WARNING)
            << LOG_DESC("onReceiveRouterSeq error") << LOG_KV("code", _e.errorCode())
            << LOG_KV("message", _e.what());
        return;
    }
    auto statusSeq = boost::asio::detail::socket_ops::network_to_host_long(
        *((uint32_t*)_message->payload()->data()));
    if (!tryToUpdateSeq(_session->p2pID(), statusSeq))
    {
        return;
    }
    SERVICE_ROUTER_LOG(INFO) << LOG_DESC("onReceiveRouterSeq and request routerTable")
                             << LOG_KV("peer", _session->p2pID()) << LOG_KV("seq", statusSeq);
    // request router table to peer
    auto dstP2PNodeID =
        (_message->srcP2PNodeID().size() > 0) ? _message->srcP2PNodeID() : _session->p2pID();
    asyncSendMessageByP2PNodeID(
        GatewayMessageType::RouterTableRequest, dstP2PNodeID, bytesConstRef());
}

void ServiceV2::onNewSession(P2PSession::Ptr _session)
{
    std::set<std::string> unreachableNodes;
    auto entry = m_routerTableFactory->createRouterEntry();
    entry->setDstNode(_session->p2pID());
    entry->setTTL(0);
    if (!m_routerTable->update(unreachableNodes, m_nodeID, entry))
    {
        SERVICE_ROUTER_LOG(INFO) << LOG_DESC("onNewSession: RouterTable not changed")
                                 << LOG_KV("dst", _session->p2pID());
        return;
    }
    onP2PNodesUnreachable(unreachableNodes);
    m_statusSeq++;
    broadcastRouterSeq();
    SERVICE_ROUTER_LOG(INFO) << LOG_DESC("onNewSession: update routerTable")
                             << LOG_KV("dst", _session->p2pID());
}

void ServiceV2::onEraseSession(P2PSession::Ptr _session)
{
    eraseSeq(_session->p2pID());
    std::set<std::string> unreachableNodes;
    if (m_routerTable->erase(unreachableNodes, _session->p2pID()))
    {
        onP2PNodesUnreachable(unreachableNodes);
        m_statusSeq++;
        broadcastRouterSeq();
    }
    SERVICE_ROUTER_LOG(INFO) << LOG_DESC("onEraseSession") << LOG_KV("dst", _session->p2pID());
}

bool ServiceV2::tryToUpdateSeq(std::string const& _p2pNodeID, uint32_t _seq)
{
    UpgradableGuard l(x_node2Seq);
    if (m_node2Seq.count(_p2pNodeID) && m_node2Seq.at(_p2pNodeID) >= _seq)
    {
        return false;
    }
    UpgradeGuard ul(l);
    m_node2Seq[_p2pNodeID] = _seq;
    return true;
}

bool ServiceV2::eraseSeq(std::string const& _p2pNodeID)
{
    UpgradableGuard l(x_node2Seq);
    if (!m_node2Seq.count(_p2pNodeID))
    {
        return false;
    }
    UpgradeGuard ul(l);
    m_node2Seq.erase(_p2pNodeID);
    return true;
}

void ServiceV2::asyncSendMessageByNodeIDWithMsgForward(
    std::shared_ptr<P2PMessage> _message, CallbackFuncWithSession _callback, Options _options)
{
    auto dstNodeID = _message->dstP2PNodeID();
    // without nextHop: maybe network unreachable or with ttl equal to 1
    auto nextHop = m_routerTable->getNextHop(dstNodeID);
    if (nextHop.size() == 0)
    {
        SERVICE_LOG(TRACE) << LOG_DESC("asyncSendMessageByNodeID: sendMessage to dstNode")
                           << LOG_KV("from", _message->srcP2PNodeID())
                           << LOG_KV("to", _message->dstP2PNodeID())
                           << LOG_KV("type", _message->packetType())
                           << LOG_KV("rsp", _message->isRespPacket());
        return Service::asyncSendMessageByNodeID(dstNodeID, _message, _callback, _options);
    }
    // with nextHop, send the message to nextHop
    SERVICE_LOG(TRACE) << LOG_DESC("asyncSendMessageByNodeID: forwardMessage to nextHop")
                       << LOG_KV("from", _message->srcP2PNodeID())
                       << LOG_KV("to", _message->dstP2PNodeID()) << LOG_KV("nextHop", nextHop)
                       << LOG_KV("type", _message->packetType())
                       << LOG_KV("rsp", _message->isRespPacket());
    return Service::asyncSendMessageByNodeID(nextHop, _message, _callback, _options);
}

void ServiceV2::asyncSendMessageByNodeID(P2pID _nodeID, std::shared_ptr<P2PMessage> _message,
    CallbackFuncWithSession _callback, Options _options)
{
    _message->setSrcP2PNodeID(m_nodeID);
    _message->setDstP2PNodeID(_nodeID);
    asyncSendMessageByNodeIDWithMsgForward(_message, _callback, _options);
}

void ServiceV2::onMessage(NetworkException _e, SessionFace::Ptr _session, Message::Ptr _message,
    std::weak_ptr<P2PSession> _p2pSessionWeakPtr)
{
    if (_e.errorCode())
    {
        SERVICE_LOG(WARNING) << LOG_DESC("onMessage error") << LOG_KV("code", _e.errorCode())
                             << LOG_KV("msg", _e.what());
        // calls onMessage of Service to trigger disconnectHandler
        Service::onMessage(_e, _session, _message, _p2pSessionWeakPtr);
        return;
    }
    // v0 message or the dstP2PNodeID is the nodeSelf
    auto p2pMsg = std::dynamic_pointer_cast<P2PMessageV2>(_message);
    if (p2pMsg->dstP2PNodeID().size() == 0 || p2pMsg->dstP2PNodeID() == m_nodeID)
    {
        SERVICE_LOG(TRACE) << LOG_DESC("onMessage") << LOG_KV("from", p2pMsg->srcP2PNodeID())
                           << LOG_KV("dst", p2pMsg->dstP2PNodeID())
                           << LOG_KV("type", p2pMsg->packetType())
                           << LOG_KV("rsp", p2pMsg->isRespPacket())
                           << LOG_KV("payLoadSize", p2pMsg->payload()->size());
        Service::onMessage(_e, _session, _message, _p2pSessionWeakPtr);
        return;
    }
    // forward the message again
    asyncSendMessageByNodeIDWithMsgForward(p2pMsg, nullptr);
}

void ServiceV2::asyncBroadcastMessage(std::shared_ptr<P2PMessage> message, Options options)
{
    auto reachableNodes = m_routerTable->getAllReachableNode();
    try
    {
        std::unordered_map<P2pID, P2PSession::Ptr> sessions;
        {
            RecursiveGuard l(x_sessions);
            std::for_each(m_sessions.begin(), m_sessions.end(),
                [&](std::unordered_map<P2pID, P2PSession::Ptr>::value_type& _value) {
                    reachableNodes.insert(_value.first);
                });
        }
        for (auto const& node : reachableNodes)
        {
            message->setSrcP2PNodeID(m_nodeID);
            message->setDstP2PNodeID(node);
            asyncSendMessageByNodeID(node, message, CallbackFuncWithSession(), options);
        }
    }
    catch (std::exception& e)
    {
        SERVICE_LOG(WARNING) << LOG_DESC("asyncBroadcastMessage")
                             << LOG_KV("what", boost::diagnostic_information(e));
    }
}

// broadcast message without forward
void ServiceV2::asyncBroadcastMessageWithoutForward(
    std::shared_ptr<P2PMessage> message, Options options)
{
    Service::asyncBroadcastMessage(message, options);
}

bool ServiceV2::isReachable(P2pID const& _nodeID) const
{
    auto reachableNodes = m_routerTable->getAllReachableNode();
    return reachableNodes.count(_nodeID);
}

void ServiceV2::sendRespMessageBySession(
    bytesConstRef _payload, P2PMessage::Ptr _p2pMessage, P2PSession::Ptr _p2pSession)
{
    auto respMessage = std::dynamic_pointer_cast<P2PMessageV2>(messageFactory()->buildMessage());
    auto requestMsg = std::dynamic_pointer_cast<P2PMessageV2>(_p2pMessage);
    respMessage->setDstP2PNodeID(requestMsg->srcP2PNodeID());
    respMessage->setSrcP2PNodeID(requestMsg->dstP2PNodeID());
    respMessage->setSeq(requestMsg->seq());
    respMessage->setRespPacket();
    respMessage->setPayload(std::make_shared<bytes>(_payload.begin(), _payload.end()));

    asyncSendMessageByNodeID(respMessage->dstP2PNodeID(), respMessage, nullptr);
    SERVICE_LOG(TRACE) << "sendRespMessageBySession" << LOG_KV("seq", requestMsg->seq())
                       << LOG_KV("from", respMessage->srcP2PNodeID())
                       << LOG_KV("dst", respMessage->dstP2PNodeID())
                       << LOG_KV("payload size", _payload.size());
}