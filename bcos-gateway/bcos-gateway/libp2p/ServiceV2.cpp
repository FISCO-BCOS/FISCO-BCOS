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
#include <bcos-task/Wait.h>
#include <cstring>
#include <utility>

using namespace bcos;
using namespace bcos::gateway;

static bool isRawP2pID(std::string const& p2pID)
{
    return p2pID.size() > HASH_NODEID_MAX_SIZE;
}

ServiceV2::ServiceV2(P2PInfo const& _p2pInfo, RouterTableFactory::Ptr _routerTableFactory,
    boost::asio::io_context& _ioContext)
  : Service(_p2pInfo),
    m_routerTimer(std::make_shared<Timer>(_ioContext, 3000, "routerSeqSync")),
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

    // FIB-186 (vector B): the timer is the trailing-edge flush and the steady-state reconciliation.
    // It resets the coalescing flag so the next membership change is a fresh leading edge, and it
    // survives a broadcast failure by re-arming on the error path -- otherwise a single throwing
    // broadcast would leave the timer un-rearmed and router-seq sync would stop silently until
    // process restart (there is no other re-arm path once inline broadcasts are coalesced).
    m_routerTimer->registerTimeoutHandler([this]() {
        m_routerSeqDirty.store(false, std::memory_order_release);
        try
        {
            broadcastRouterSeq();
        }
        catch (std::exception const& e)
        {
            SERVICE2_LOG(WARNING) << LOG_BADGE("routerSeqSync")
                                  << LOG_DESC("broadcastRouterSeq exception")
                                  << LOG_KV("error", e.what());
            m_routerTimer->restart();
        }
    });
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
    markRouterSeqChanged();
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
    auto self = std::static_pointer_cast<ServiceV2>(shared_from_this());
    // fire-and-forget through the coroutine fast path: the message is built in the frame and the
    // router table payload is moved into it (the caller's buffer does not outlive the deferred
    // send); an unreachable peer is an expected, recoverable state.
    task::wait([](std::shared_ptr<ServiceV2> _self, uint16_t _type, P2pID _nodeID,
                   bcos::bytes _payload) -> task::Task<void> {
        P2PMessageV2 message;
        message.setPacketType(_type);
        message.setSeq(_self->messageFactory()->newSeq());
        message.setPayload(std::move(_payload));
        try
        {
            co_await _self->sendMessageByNodeID(_nodeID, message,
                ::ranges::views::single(message.payload()), Options{0, false});
        }
        catch (NetworkException const& e)
        {
            SERVICE2_LOG(INFO)
                << LOG_DESC("onReceiveRouterTableRequest send RouterTableResponse failed")
                << LOG_KV("nodeid", printShortP2pID(_nodeID)) << LOG_KV("code", e.errorCode())
                << LOG_KV("msg", e.what());
        }
    }(self, GatewayMessageType::RouterTableResponse, dstP2PNodeID, std::move(*routerTableData)));
}

void ServiceV2::broadcastRouterSeq()
{
    m_routerTimer->restart();
    auto seq = m_statusSeq.load();
    auto statusSeq = boost::asio::detail::socket_ops::host_to_network_long(seq);
    bytes payload;
    payload.insert(payload.end(), (byte*)&statusSeq, (byte*)&statusSeq + 4);
    auto self = std::static_pointer_cast<ServiceV2>(shared_from_this());
    // value message held by shared_ptr; the 4-byte seq payload is owned by it (zero-copy view
    // send). The router table should only be exchanged between neighbours and propagated
    // hop-by-hop, so broadcast to the directly connected sessions only (not all reachable nodes).
    // All state is passed as coroutine parameters so it is copied into the frame and stays alive.
    task::wait([](std::shared_ptr<ServiceV2> _self, bcos::bytes _payload) mutable
                   -> task::Task<void> {
        auto message = std::make_shared<P2PMessageV2>();
        message->setPacketType(GatewayMessageType::RouterTableSyncSeq);
        message->setPayload(std::move(_payload));
        co_await _self->broadcastMessageToNeighbors(
            message, ::ranges::views::single(message->payload()), Options{});
    }(self, std::move(payload)));
}

void ServiceV2::markRouterSeqChanged()
{
    // FIB-186 (vector B): leading-edge coalesce. Broadcast the seq immediately on the first change
    // of a burst -- neighbours then re-request the current full router table, which already
    // reflects the changes made so far -- so a directly reachable peer converges without waiting on
    // the timer. Further churn within the window only marks the flag (no broadcast); the
    // m_routerTimer flushes the coalesced state once and resets the flag. The pre-fix code
    // broadcast on every single membership/route change, so connect/disconnect churn cascaded a
    // full-mesh seq->request->whole-table gossip on m_asyncGroup -- the pool that delivers PBFT
    // messages -- and starved consensus (CertiK vector B). broadcastRouterSeq() also restart()s the
    // timer, which resurrects it if a prior tick failed to re-arm.
    if (!m_routerSeqDirty.exchange(true, std::memory_order_acq_rel))
    {
        broadcastRouterSeq();
    }
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
    // FIB-183: the router-sequence payload must contain at least a 4-byte sequence number.
    // A short or empty payload (the smallest attacker-supplied frames are 14-78 bytes total)
    // would read past the end of the decoded payload buffer. Drop it before dereferencing.
    if (_message->payload().size() < sizeof(uint32_t))
    {
        SERVICE2_LOG(WARNING) << LOG_BADGE("onReceiveRouterSeq") << LOG_DESC("short payload, drop")
                              << LOG_KV("size", _message->payload().size());
        return;
    }
    uint32_t seq = 0;
    std::memcpy(&seq, _message->payload().data(), sizeof(seq));
    auto statusSeq = boost::asio::detail::socket_ops::network_to_host_long(seq);
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
    auto self = std::static_pointer_cast<ServiceV2>(shared_from_this());
    // fire-and-forget through the coroutine fast path: the message is built in the frame and the
    // (empty) payload rides as a view; an unreachable peer is an expected, recoverable state.
    task::wait([](std::shared_ptr<ServiceV2> _self, uint16_t _type, P2pID _nodeID)
                   -> task::Task<void> {
        P2PMessageV2 message;
        message.setPacketType(_type);
        message.setSeq(_self->messageFactory()->newSeq());
        try
        {
            co_await _self->sendMessageByNodeID(_nodeID, message,
                ::ranges::views::single(message.payload()), Options{0, false});
        }
        catch (NetworkException const& e)
        {
            SERVICE2_LOG(INFO) << LOG_DESC("onReceiveRouterSeq send RouterTableRequest failed")
                               << LOG_KV("nodeid", printShortP2pID(_nodeID))
                               << LOG_KV("code", e.errorCode()) << LOG_KV("msg", e.what());
        }
    }(self, GatewayMessageType::RouterTableRequest, dstP2PNodeID));
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
    markRouterSeqChanged();
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
        markRouterSeqChanged();
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
        SERVICE2_LOG(TRACE) << LOG_BADGE("onMessage") << LOG_DESC("forwardMessage")
                            << LOG_KV("seq", p2pMsg->seq())
                            << LOG_KV("from", p2pMsg->printSrcP2PNodeID())
                            << LOG_KV("dst", p2pMsg->printDstP2PNodeID())
                            << LOG_KV("type", p2pMsg->packetType()) << LOG_KV("seq", p2pMsg->seq())
                            << LOG_KV("rsp", p2pMsg->isRespPacket()) << LOG_KV("ttl", p2pMsg->ttl())
                            << LOG_KV("payLoadSize", p2pMsg->payload().size());
    }
    // forward through the coroutine fast path (zero-copy: the received message is passed as a
    // coroutine parameter so it is copied into the frame and stays alive, and its payload is sent
    // as a view). Note: forwarding must NOT rewrite srcP2PNodeID (that is only done when this node
    // originates the message) — forwardMessageByNodeID only resolves the next hop.
    auto self = std::static_pointer_cast<ServiceV2>(shared_from_this());
    task::wait([](std::shared_ptr<ServiceV2> _self,
                   std::shared_ptr<P2PMessageV2> _p2pMsg) -> task::Task<void> {
        try
        {
            co_await _self->forwardMessageByNodeID(_p2pMsg->dstP2PNodeID(), *_p2pMsg,
                ::ranges::views::single(_p2pMsg->payload()), Options{});
        }
        catch (std::exception const& e)
        {
            // A synchronous pre-send rejection (rate limit / max size) or an async write failure
            // on the relay path is expected during bandwidth saturation — log the next hop so a
            // "peer not receiving relayed messages" investigation keeps the routing dimension.
            SERVICE2_LOG(WARNING) << LOG_BADGE("onMessage") << LOG_DESC("forwardMessage failed")
                                  << LOG_KV("dst", _p2pMsg->dstP2PNodeID())
                                  << LOG_KV("seq", _p2pMsg->seq())
                                  << LOG_KV("what", boost::diagnostic_information(e));
        }
    }(self, p2pMsg));
}

bcos::task::Task<void> ServiceV2::broadcastMessageToAll(P2PMessage::Ptr message,
    ::ranges::any_view<bytesConstRef, ::ranges::category::forward> payloads, Options options)
{
    auto reachableNodes = m_routerTable->getAllReachableNode();
    auto selfV2 = std::static_pointer_cast<ServiceV2>(shared_from_this());
    // Fan out one independent coroutine per peer (see Service::broadcastMessageToAll).
    for (auto const& node : reachableNodes)
    {
        task::wait([](std::shared_ptr<ServiceV2> _self, P2pID _node, P2PMessage::Ptr _message,
                       ::ranges::any_view<bytesConstRef, ::ranges::category::forward> _payloads,
                       Options _options) mutable -> task::Task<void> {
            try
            {
                co_await _self->sendMessageByNodeID(
                    _node, *_message, std::move(_payloads), std::move(_options));
            }
            catch (std::exception const& e)
            {
                SERVICE2_LOG(WARNING) << LOG_BADGE("broadcastMessageToAll")
                                      << LOG_KV("node", printShortP2pID(_node))
                                      << LOG_KV("what", boost::diagnostic_information(e));
            }
        }(selfV2, node, message, payloads, options));
    }
    co_return;
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
    auto self = shared_from_this();
    auto requestMsg = std::dynamic_pointer_cast<P2PMessageV2>(_p2pMessage);
    auto seq = requestMsg->seq();
    auto dstP2PNodeID = requestMsg->srcP2PNodeID();
    auto p2pid = _p2pSession->p2pID();
    // value message in frame; response payload copied into the frame (borrowed from the receive
    // callback which does not outlive the deferred send). All state is passed as coroutine
    // parameters so it is copied into the frame and stays alive.
    task::wait([](std::shared_ptr<Service> _self, P2PSession::Ptr _p2pSession,
                   bcos::bytes _payload, uint32_t _seq, std::string _dstP2PNodeID,
                   P2pID _p2pid) -> task::Task<void> {
        try
        {
            P2PMessageV2 respMessage;
            respMessage.setDstP2PNodeID(_dstP2PNodeID);
            respMessage.setSrcP2PNodeID(_self->m_nodeID);
            respMessage.setSeq(_seq);
            respMessage.setRespPacket();
            respMessage.setPayload(std::move(_payload));
            // Note: respond directly via the original session (zero-copy view)
            co_await _p2pSession->fastSendP2PMessage(
                respMessage, ::ranges::views::single(respMessage.payload()), Options{});
            if (c_fileLogLevel <= TRACE) [[unlikely]]
            {
                SERVICE2_LOG(TRACE) << LOG_BADGE("sendRespMessageBySession")
                                    << LOG_KV("seq", _seq)
                                    << LOG_KV("from", respMessage.printSrcP2PNodeID())
                                    << LOG_KV("dst", respMessage.printDstP2PNodeID())
                                    << LOG_KV("payload size", respMessage.payload().size());
            }
        }
        catch (std::exception const& e)
        {
            // A synchronous pre-send rejection (rate limit / max size) on the response path is
            // expected during bandwidth saturation — log the request seq and target so the
            // response-loss investigation keeps the routing dimension.
            SERVICE2_LOG(WARNING) << LOG_BADGE("sendRespMessageBySession")
                                  << LOG_DESC("send response failed") << LOG_KV("seq", _seq)
                                  << LOG_KV("dst", _dstP2PNodeID)
                                  << LOG_KV("what", boost::diagnostic_information(e));
        }
    }(self, _p2pSession, bcos::bytes(_payload.begin(), _payload.end()), seq, dstP2PNodeID,
        p2pid));
}

bcos::task::Task<Message::Ptr> bcos::gateway::ServiceV2::sendMessageByNodeID(
    P2pID nodeID, P2PMessage& header, ::ranges::any_view<bytesConstRef> payloads, Options options)
{
    // this node originates the message: stamp src/dst before routing
    header.setSrcP2PNodeID(m_nodeID);
    header.setDstP2PNodeID(nodeID);

    co_return co_await forwardMessageByNodeID(nodeID, header, std::move(payloads), options);
}

bcos::task::Task<Message::Ptr> bcos::gateway::ServiceV2::forwardMessageByNodeID(
    P2pID nodeID, P2PMessage& header, ::ranges::any_view<bytesConstRef> payloads, Options options)
{
    // Forwarding path (a message received from another node being relayed): unlike
    // sendMessageByNodeID it must NOT rewrite srcP2PNodeID — the original sender is preserved so
    // the final destination can reply directly to it. Only the next hop is resolved here.
    auto dstNodeID = header.dstP2PNodeID();
    // without nextHop: maybe network unreachable or with distance equal to 1
    auto nextHop = m_routerTable->getNextHop(dstNodeID);
    if (nextHop.empty())
    {
        if (c_fileLogLevel == TRACE) [[unlikely]]
        {
            SERVICE2_LOG(TRACE) << LOG_BADGE("forwardMessageByNodeID")
                                << LOG_DESC("sendMessage to dstNode")
                                << LOG_KV("from", header.printSrcP2PNodeID())
                                << LOG_KV("to", header.printDstP2PNodeID())
                                << LOG_KV("type", header.packetType())
                                << LOG_KV("seq", header.seq())
                                << LOG_KV("rsp", header.isRespPacket());
        }
        co_return co_await Service::sendMessageByNodeID(
            std::move(dstNodeID), header, std::move(payloads), options);
    }
    // with nextHop, send the message to nextHop
    if (c_fileLogLevel == TRACE) [[unlikely]]
    {
        SERVICE2_LOG(TRACE) << LOG_BADGE("forwardMessageByNodeID")
                            << LOG_DESC("forwardMessage to nextHop")
                            << LOG_KV("from", header.printSrcP2PNodeID())
                            << LOG_KV("to", header.printDstP2PNodeID())
                            << LOG_KV("nextHop", printShortP2pID(nextHop))
                            << LOG_KV("type", header.packetType()) << LOG_KV("seq", header.seq())
                            << LOG_KV("rsp", header.isRespPacket());
    }
    co_return co_await Service::sendMessageByNodeID(
        std::move(nextHop), header, std::move(payloads), options);
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
