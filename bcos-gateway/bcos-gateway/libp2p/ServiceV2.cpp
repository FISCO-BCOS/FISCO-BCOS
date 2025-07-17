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
#include "bcos-utilities/BoostLog.h"
#include <utility>

using namespace bcos;
using namespace bcos::gateway;

static bool isRawP2pID(std::string const& p2pID)
{
    return p2pID.size() > HASH_NODEID_MAX_SIZE;
}

ServiceV2::ServiceV2(P2PInfo const& _p2pInfo, RouterTableFactory::Ptr _routerTableFactory)
  : Service(_p2pInfo),
    m_routerTimer(std::make_shared<Timer>(3000, "routerSeqSync")),
    m_routerTableFactory(std::move(_routerTableFactory)),
    m_routerTable(m_routerTableFactory->createRouterTable())

{
    updateP2pInfo(m_selfInfo);
    m_routerTable->setNodeID(m_nodeID);
    m_routerTable->setUnreachableDistance(c_unreachableDistance);
    // process router packet related logic
    registerHandlerByMsgType(GatewayMessageType::RouterTableSyncSeq,
        [this](NetworkException exception, std::shared_ptr<P2PSession> session,
            P2PMessage::Ptr message) {
            onReceiveRouterSeq(std::move(exception), std::move(session), std::move(message));
        });
    registerHandlerByMsgType(GatewayMessageType::RouterTableResponse,
        [this](NetworkException exception, std::shared_ptr<P2PSession> session,
            P2PMessage::Ptr message) {
            onReceivePeersRouterTable(std::move(exception), std::move(session), std::move(message));
        });

    registerHandlerByMsgType(GatewayMessageType::RouterTableRequest,
        [this](NetworkException exception, std::shared_ptr<P2PSession> session,
            P2PMessage::Ptr message) {
            onReceiveRouterTableRequest(
                std::move(exception), std::move(session), std::move(message));
        });
    registerOnNewSession([this](P2PSession::Ptr _session) { onNewSession(std::move(_session)); });
    registerOnDeleteSession(
        [this](P2PSession::Ptr _session) { onEraseSession(std::move(_session)); });

    m_routerTimer->registerTimeoutHandler([this]() { broadcastRouterSeq(); });
}

void ServiceV2::start()
{
    Service::start();
    m_routerTimer->start();
}

void ServiceV2::stop()
{
    m_routerTimer->stop();
    Service::stop();
}

// receive routerTable from peers
void ServiceV2::onReceivePeersRouterTable(
    NetworkException _error, std::shared_ptr<P2PSession> _session, P2PMessage::Ptr _message)
{
    if (_error.errorCode() != 0)
    {
        SERVICE2_LOG(WARNING) << LOG_BADGE("onReceivePeersRouterTable")
                              << LOG_KV("code", _error.errorCode()) << LOG_KV("msg", _error.what());
        return;
    }
    auto routerTable = m_routerTableFactory->createRouterTable(_message->payload());

    SERVICE2_LOG(INFO) << LOG_BADGE("onReceivePeersRouterTable")
                       << LOG_KV("peer", _session->printP2pID())
                       << LOG_KV("entrySize", routerTable->routerEntries().size());
    joinRouterTable(_session, routerTable);
}

void ServiceV2::joinRouterTable(
    std::shared_ptr<P2PSession> _session, RouterTableInterface::Ptr _routerTable)
{
    auto generatedFrom = _session->p2pID();
    std::set<std::string> unreachableNodes;
    bool updated = false;
    auto const& entries = _routerTable->routerEntries();
    for (auto const& it : entries)
    {
        auto dstNodeInfo = it.second->dstNodeInfo();
        // old-node case, without p2pID, try to find locally
        if (dstNodeInfo.p2pID.empty())
        {
            getRawP2pID(dstNodeInfo.p2pID);
            it.second->setDstNodeInfo(dstNodeInfo);
        }
        auto entry = it.second;
        if (m_routerTable->update(unreachableNodes, generatedFrom, entry) && !updated)
        {
            updated = true;
        }
        // update the nodeInfo
        updateP2pInfo(dstNodeInfo);
    }

    SERVICE2_LOG(INFO) << LOG_BADGE("joinRouterTable") << LOG_DESC("create router entry")
                       << LOG_KV("dst", printShortP2pID(generatedFrom));

    auto entry = m_routerTableFactory->createRouterEntry();
    entry->setDstNode(_session->p2pID());
    entry->setDstNodeInfo(_session->p2pInfo());
    entry->setDistance(0);
    if (m_routerTable->update(unreachableNodes, m_nodeID, entry) && !updated)
    {
        updated = true;
    }
    if (!updated)
    {
        SERVICE2_LOG(DEBUG) << LOG_BADGE("joinRouterTable") << LOG_DESC("router table not updated")
                            << LOG_KV("dst", printShortP2pID(generatedFrom));
        return;
    }
    onP2PNodesUnreachable(unreachableNodes);
    m_statusSeq++;
    broadcastRouterSeq();
}

// receive routerTable request from peer
void ServiceV2::onReceiveRouterTableRequest(
    NetworkException _error, std::shared_ptr<P2PSession> _session, P2PMessage::Ptr _message)
{
    if (_error.errorCode() != 0)
    {
        SERVICE2_LOG(WARNING) << LOG_BADGE("onReceiveRouterTableRequest")
                              << LOG_KV("code", _error.errorCode()) << LOG_KV("msg", _error.what());
        return;
    }
    SERVICE2_LOG(INFO) << LOG_BADGE("onReceiveRouterTableRequest")
                       << LOG_KV("peer", _session->printP2pID())
                       << LOG_KV("entrySize", m_routerTable->routerEntries().size());

    auto routerTableData = std::make_shared<bytes>();
    m_routerTable->encode(*routerTableData);
    auto dstP2PNodeID =
        (!_message->srcP2PNodeID().empty()) ? _message->srcP2PNodeID() : _session->p2pID();
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
    message->setPayload({(byte*)&statusSeq, (byte*)&statusSeq + 4});
    // the router table should only exchange between neighbor
    asyncBroadcastMessageWithoutForward(message, Options());
}

void ServiceV2::onReceiveRouterSeq(
    NetworkException _error, std::shared_ptr<P2PSession> _session, P2PMessage::Ptr _message)
{
    if (_error.errorCode() != 0)
    {
        SERVICE2_LOG(WARNING) << LOG_BADGE("onReceiveRouterSeq")
                              << LOG_KV("code", _error.errorCode())
                              << LOG_KV("message", _error.what());
        return;
    }
    auto statusSeq = boost::asio::detail::socket_ops::network_to_host_long(
        *((uint32_t*)_message->payload().data()));
    if (!tryToUpdateSeq(_session->p2pID(), statusSeq))
    {
        return;
    }
    SERVICE2_LOG(INFO) << LOG_BADGE("onReceiveRouterSeq")
                       << LOG_DESC("receive router seq and request router table")
                       << LOG_KV("peer", _session->printP2pID()) << LOG_KV("seq", statusSeq);
    // request router table to peer
    auto dstP2PNodeID =
        (!_message->srcP2PNodeID().empty()) ? _message->srcP2PNodeID() : _session->p2pID();
    asyncSendMessageByP2PNodeID(
        GatewayMessageType::RouterTableRequest, dstP2PNodeID, bytesConstRef());
}

void ServiceV2::onNewSession(P2PSession::Ptr _session)
{
    // update the p2p information when establish new session
    updateP2pInfo(_session->p2pInfo());
    std::set<std::string> unreachableNodes;
    auto entry = m_routerTableFactory->createRouterEntry();
    entry->setDstNode(_session->p2pID());
    entry->setDstNodeInfo(_session->p2pInfo());
    entry->setDistance(0);
    if (!m_routerTable->update(unreachableNodes, m_nodeID, entry))
    {
        SERVICE2_LOG(INFO) << LOG_BADGE("onNewSession") << LOG_DESC("routerTable not changed")
                           << LOG_KV("dst", _session->printP2pID());
        return;
    }
    onP2PNodesUnreachable(unreachableNodes);
    m_statusSeq++;
    broadcastRouterSeq();
    SERVICE2_LOG(INFO) << LOG_BADGE("onNewSession") << LOG_DESC("update routerTable")
                       << LOG_KV("dst", _session->printP2pID());
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
    SERVICE2_LOG(INFO) << LOG_BADGE("onEraseSession") << LOG_KV("dst", _session->printP2pID());
}

bool ServiceV2::tryToUpdateSeq(std::string const& _p2pNodeID, uint32_t _seq)
{
    UpgradableGuard l(x_node2Seq);
    auto it = m_node2Seq.find(_p2pNodeID);
    if (it != m_node2Seq.end() && it->second >= _seq)
    {
        return false;
    }
    UpgradeGuard upgradeGuard(l);
    m_node2Seq[_p2pNodeID] = _seq;
    return true;
}

bool ServiceV2::eraseSeq(std::string const& _p2pNodeID)
{
    UpgradableGuard l(x_node2Seq);
    auto it = m_node2Seq.find(_p2pNodeID);
    if (it == m_node2Seq.end())
    {
        return false;
    }
    UpgradeGuard upgradeGuard(l);
    m_node2Seq.erase(it);
    return true;
}

void ServiceV2::asyncSendMessageByNodeIDWithMsgForward(
    std::shared_ptr<P2PMessage> _message, CallbackFuncWithSession _callback, Options _options)
{
    auto dstNodeID = _message->dstP2PNodeID();
    // without nextHop: maybe network unreachable or with distance equal to 1
    auto nextHop = m_routerTable->getNextHop(dstNodeID);
    if (nextHop.empty())
    {
        if (c_fileLogLevel == TRACE) [[unlikely]]
        {
            SERVICE2_LOG(TRACE) << LOG_BADGE("asyncSendMessageByNodeIDWithMsgForward")
                                << LOG_DESC("sendMessage to dstNode")
                                << LOG_KV("from", _message->printSrcP2PNodeID())
                                << LOG_KV("to", _message->printDstP2PNodeID())
                                << LOG_KV("type", _message->packetType())
                                << LOG_KV("seq", _message->seq())
                                << LOG_KV("rsp", _message->isRespPacket());
        }
        return Service::asyncSendMessageByNodeID(dstNodeID, _message, _callback, _options);
    }
    // with nextHop, send the message to nextHop
    if (c_fileLogLevel == TRACE) [[unlikely]]
    {
        SERVICE2_LOG(TRACE) << LOG_BADGE("asyncSendMessageByNodeIDWithMsgForward")
                            << LOG_DESC("forwardMessage to nextHop")
                            << LOG_KV("from", _message->printSrcP2PNodeID())
                            << LOG_KV("to", _message->printDstP2PNodeID())
                            << LOG_KV("nextHop", printShortP2pID(nextHop))
                            << LOG_KV("type", _message->packetType())
                            << LOG_KV("seq", _message->seq())
                            << LOG_KV("rsp", _message->isRespPacket());
    }
    return Service::asyncSendMessageByNodeID(nextHop, _message, _callback, _options);
}

void ServiceV2::asyncSendMessageByNodeID(P2pID _nodeID, std::shared_ptr<P2PMessage> _message,
    CallbackFuncWithSession _callback, Options _options)
{
    _message->setSrcP2PNodeID(m_nodeID);
    _message->setDstP2PNodeID(_nodeID);
    asyncSendMessageByNodeIDWithMsgForward(_message, _callback, _options);
}

void ServiceV2::onMessage(NetworkException _error, SessionFace::Ptr _session, Message::Ptr _message,
    std::weak_ptr<P2PSession> _p2pSessionWeakPtr)
{
    if (_error.errorCode() != 0)
    {
        SERVICE2_LOG(WARNING) << LOG_BADGE("onMessage") << LOG_KV("code", _error.errorCode())
                              << LOG_KV("msg", _error.what());
        // calls onMessage of Service to trigger disconnectHandler
        Service::onMessage(_error, _session, _message, _p2pSessionWeakPtr);
        return;
    }
    // v0 message or the dstP2PNodeID is the nodeSelf or empty
    auto p2pMsg = std::dynamic_pointer_cast<P2PMessageV2>(_message);
    auto dstNodeP2pID = getRawP2pID(p2pMsg->dstP2PNodeID());
    if (p2pMsg->dstP2PNodeID().empty() || dstNodeP2pID == m_nodeID)
    {
        if (c_fileLogLevel <= TRACE) [[unlikely]]
        {
            SERVICE2_LOG(TRACE) << LOG_BADGE("onMessage")
                                << LOG_KV("from", p2pMsg->printSrcP2PNodeID())
                                << LOG_KV("seq", p2pMsg->seq())
                                << LOG_KV("dst", p2pMsg->printDstP2PNodeID())
                                << LOG_KV("type", p2pMsg->packetType())
                                << LOG_KV("rsp", p2pMsg->isRespPacket())
                                << LOG_KV("ttl", p2pMsg->ttl())
                                << LOG_KV("payLoadSize", p2pMsg->payload().size());
        }
        // convert short-p2p-id to long-p2p-id when handle the message
        p2pMsg->setDstP2PNodeID(dstNodeP2pID);
        p2pMsg->setSrcP2PNodeID(getRawP2pID(p2pMsg->srcP2PNodeID()));
        Service::onMessage(_error, _session, _message, _p2pSessionWeakPtr);
        return;
    }
    // forward the message again
    auto ttl = (int16_t)p2pMsg->ttl();
    if (ttl <= 0)
    {
        SERVICE2_LOG(WARNING) << LOG_BADGE("onMessage") << LOG_DESC("expired ttl")
                              << LOG_KV("seq", p2pMsg->seq())
                              << LOG_KV("from", p2pMsg->printSrcP2PNodeID())
                              << LOG_KV("dst", p2pMsg->dstP2PNodeID())
                              << LOG_KV("type", p2pMsg->packetType())
                              << LOG_KV("rsp", p2pMsg->isRespPacket())
                              << LOG_KV("payLoadSize", p2pMsg->payload().size())
                              << LOG_KV("ttl", ttl);
        return;
    }
    ttl -= 1;
    // recover to long p2p-node id for dispatcher through router
    p2pMsg->setDstP2PNodeID(dstNodeP2pID);
    p2pMsg->setTTL(ttl);
    if (c_fileLogLevel <= TRACE) [[unlikely]]
    {
        SERVICE2_LOG(TRACE) << LOG_BADGE("onMessage")
                            << LOG_DESC("asyncSendMessageByNodeIDWithMsgForward")
                            << LOG_KV("seq", p2pMsg->seq())
                            << LOG_KV("from", p2pMsg->printSrcP2PNodeID())
                            << LOG_KV("dst", p2pMsg->printDstP2PNodeID())
                            << LOG_KV("type", p2pMsg->packetType()) << LOG_KV("seq", p2pMsg->seq())
                            << LOG_KV("rsp", p2pMsg->isRespPacket()) << LOG_KV("ttl", p2pMsg->ttl())
                            << LOG_KV("payLoadSize", p2pMsg->payload().size());
    }
    asyncSendMessageByNodeIDWithMsgForward(p2pMsg, nullptr);
}

void ServiceV2::asyncBroadcastMessage(std::shared_ptr<P2PMessage> message, Options options)
{
    auto reachableNodes = m_routerTable->getAllReachableNode();
    try
    {
        for (auto const& node : reachableNodes)
        {
            message->setSrcP2PNodeID(m_nodeID);
            message->setDstP2PNodeID(node);
            asyncSendMessageByNodeID(node, message, CallbackFuncWithSession(), options);
        }
    }
    catch (std::exception& e)
    {
        SERVICE2_LOG(WARNING) << LOG_BADGE("asyncBroadcastMessage")
                              << LOG_KV("what", boost::diagnostic_information(e));
    }
}

// broadcast message without forward
void ServiceV2::asyncBroadcastMessageWithoutForward(
    std::shared_ptr<P2PMessage> message, Options options)
{
    Service::asyncBroadcastMessage(std::move(message), options);
}

bool ServiceV2::isReachable(P2pID const& _nodeID) const
{
    auto reachableNodes = m_routerTable->getAllReachableNode();
    return reachableNodes.contains(_nodeID);
}

void ServiceV2::sendRespMessageBySession(
    bytesConstRef _payload, P2PMessage::Ptr _p2pMessage, P2PSession::Ptr _p2pSession)
{
    auto version = _p2pSession->protocolInfo()->version();
    if (version <= bcos::protocol::ProtocolVersion::V0)
    {
        Service::sendRespMessageBySession(_payload, _p2pMessage, _p2pSession);
        return;
    }
    auto respMessage = std::dynamic_pointer_cast<P2PMessageV2>(messageFactory()->buildMessage());
    auto requestMsg = std::dynamic_pointer_cast<P2PMessageV2>(_p2pMessage);
    respMessage->setDstP2PNodeID(requestMsg->srcP2PNodeID());
    respMessage->setSrcP2PNodeID(m_nodeID);
    respMessage->setSeq(requestMsg->seq());
    respMessage->setRespPacket();
    // TODO: reduce memory copy
    respMessage->setPayload({_payload.begin(), _payload.end()});

    // Note: send response directly with the original session
    sendMessageToSession(_p2pSession, respMessage);

    if (c_fileLogLevel <= TRACE) [[unlikely]]
    {
        SERVICE2_LOG(TRACE) << LOG_BADGE("sendRespMessageBySession")
                            << LOG_KV("seq", requestMsg->seq())
                            << LOG_KV("from", respMessage->printSrcP2PNodeID())
                            << LOG_KV("dst", respMessage->printDstP2PNodeID())
                            << LOG_KV("payload size", _payload.size());
    }
}

bcos::task::Task<Message::Ptr> bcos::gateway::ServiceV2::sendMessageByNodeID(
    P2pID nodeID, P2PMessage& message, ::ranges::any_view<bytesConstRef> payloads, Options options)
{
    message.setSrcP2PNodeID(m_nodeID);
    message.setDstP2PNodeID(nodeID);

    auto dstNodeID = message.dstP2PNodeID();
    // without nextHop: maybe network unreachable or with distance equal to 1
    auto nextHop = m_routerTable->getNextHop(dstNodeID);
    if (nextHop.empty())
    {
        if (c_fileLogLevel == TRACE) [[unlikely]]
        {
            SERVICE2_LOG(TRACE) << LOG_BADGE("sendMessageByNodeID")
                                << LOG_DESC("sendMessage to dstNode")
                                << LOG_KV("from", message.printSrcP2PNodeID())
                                << LOG_KV("to", message.printDstP2PNodeID())
                                << LOG_KV("type", message.packetType())
                                << LOG_KV("seq", message.seq())
                                << LOG_KV("rsp", message.isRespPacket());
        }
        co_return co_await Service::sendMessageByNodeID(
            std::move(dstNodeID), message, std::move(payloads), options);
    }
    // with nextHop, send the message to nextHop
    if (c_fileLogLevel == TRACE) [[unlikely]]
    {
        SERVICE2_LOG(TRACE) << LOG_BADGE("sendMessageByNodeID")
                            << LOG_DESC("forwardMessage to nextHop")
                            << LOG_KV("from", message.printSrcP2PNodeID())
                            << LOG_KV("to", message.printDstP2PNodeID())
                            << LOG_KV("nextHop", printShortP2pID(nextHop))
                            << LOG_KV("type", message.packetType()) << LOG_KV("seq", message.seq())
                            << LOG_KV("rsp", message.isRespPacket());
    }
    co_return co_await Service::sendMessageByNodeID(
        std::move(nextHop), message, std::move(payloads), options);
}

void bcos::gateway::ServiceV2::registerUnreachableHandler(std::function<void(std::string)> _handler)
{
    WriteGuard writeGuard(x_unreachableHandlers);
    m_unreachableHandlers.emplace_back(_handler);
}

std::string bcos::gateway::ServiceV2::getShortP2pID(std::string const& rawP2pID) const
{
    if (rawP2pID.empty())
    {
        return rawP2pID;
    }
    // already the short p2pId
    if (!isRawP2pID(rawP2pID))
    {
        return rawP2pID;
    }
    bcos::ReadGuard lock(x_rawP2pIDInfo);
    if (auto it = m_rawP2pIDInfo.find(rawP2pID); it != m_rawP2pIDInfo.end())
    {
        return it->second;
    }
    // note: in the case of running old node with the new node, the shortP2pID maybe not found
    SERVICE2_LOG(TRACE) << LOG_DESC("getShortP2pID failed, return rawP2pID directly")
                        << LOG_KV("id", printShortP2pID(rawP2pID));
    return rawP2pID;
}

std::string bcos::gateway::ServiceV2::getRawP2pID(std::string const& shortP2pID) const
{
    if (shortP2pID.empty())
    {
        return shortP2pID;
    }
    // the old node case
    if (isRawP2pID(shortP2pID))
    {
        return shortP2pID;
    }
    bcos::ReadGuard lock(x_p2pIDInfo);
    if (auto it = m_p2pIDInfo.find(shortP2pID); it != m_p2pIDInfo.end())
    {
        return it->second;
    }
    SERVICE2_LOG(WARNING) << LOG_DESC("getRawP2pID failed, return shortP2pID directly")
                          << LOG_KV("id", printShortP2pID(shortP2pID));
    return shortP2pID;
}

void bcos::gateway::ServiceV2::resetP2pID(
    P2PMessage& message, bcos::protocol::ProtocolVersion const& version)
{
    // old node case, set to long nodeID
    if (version < protocol::ProtocolVersion::V3) [[unlikely]]
    {
        message.setSrcP2PNodeID(getRawP2pID(message.srcP2PNodeID()));
        message.setDstP2PNodeID(getRawP2pID(message.dstP2PNodeID()));
        return;
    }
    // new ndoe case, set to short nodeID
    message.setSrcP2PNodeID(getShortP2pID(message.srcP2PNodeID()));
    message.setDstP2PNodeID(getShortP2pID(message.dstP2PNodeID()));
}

void bcos::gateway::ServiceV2::onP2PNodesUnreachable(std::set<std::string> const& _p2pNodeIDs)
{
    std::vector<std::function<void(std::string)>> handlers;
    {
        ReadGuard readGuard(x_unreachableHandlers);
        handlers = m_unreachableHandlers;
    }
    // TODO: async here
    for (auto const& node : _p2pNodeIDs)
    {
        for (auto const& it : m_unreachableHandlers)
        {
            it(node);
        }
    }
}

void bcos::gateway::ServiceV2::updateP2pInfo(P2PInfo const& p2pInfo)
{
    SERVICE2_LOG(INFO) << LOG_DESC("try to updateP2pInfo")
                       << LOG_KV("p2pID", printShortP2pID(p2pInfo.p2pID))
                       << LOG_KV("rawP2pID", printShortP2pID(p2pInfo.rawP2pID));
    if (p2pInfo.rawP2pID.empty() || p2pInfo.p2pID.empty())
    {
        return;
    }
    tryToUpdateRawP2pInfo(p2pInfo);
    tryToUpdateP2pInfo(p2pInfo);
}

void bcos::gateway::ServiceV2::tryToUpdateRawP2pInfo(P2PInfo const& p2pInfo)
{
    bcos::UpgradableGuard lock(x_rawP2pIDInfo);
    auto it = m_rawP2pIDInfo.find(p2pInfo.rawP2pID);
    if (it != m_rawP2pIDInfo.end())
    {
        return;
    }
    m_rawP2pIDInfo.insert(std::make_pair(p2pInfo.rawP2pID, p2pInfo.p2pID));
}

void bcos::gateway::ServiceV2::tryToUpdateP2pInfo(P2PInfo const& p2pInfo)
{
    bcos::UpgradableGuard lock(x_p2pIDInfo);
    auto it = m_p2pIDInfo.find(p2pInfo.p2pID);
    if (it != m_p2pIDInfo.end())
    {
        return;
    }
    m_p2pIDInfo.insert(std::make_pair(p2pInfo.p2pID, p2pInfo.rawP2pID));
}
