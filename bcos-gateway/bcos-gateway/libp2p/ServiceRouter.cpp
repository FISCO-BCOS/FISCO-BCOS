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
 * @file ServiceRouter.cpp
 * @brief The optional router (RIP) module of Service, merged from the former ServiceV2 subclass:
 *        router-table gossip (seq broadcast / request / response), multi-hop forwarding and the
 *        raw<->short p2pID translation maps. Enabled when Service is constructed with a
 *        RouterTableFactory; all state lives in Service::RouterState (ServiceRouter.h).
 */
#include "ServiceRouter.h"
#include "Common.h"
#include "bcos-gateway/libp2p/Message.h"
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

void Service::initRouter(
    std::shared_ptr<RouterTableFactory> _routerTableFactory, boost::asio::io_context& _ioContext)
{
    m_router = std::make_unique<RouterState>();
    m_router->routerTimer = std::make_shared<Timer>(_ioContext, 3000, "routerSeqSync");
    m_router->routerTableFactory = std::move(_routerTableFactory);
    m_router->routerTable = m_router->routerTableFactory->createRouterTable();

    updateP2pInfo(m_selfInfo);
    m_router->routerTable->setNodeID(m_nodeID);
    m_router->routerTable->setUnreachableDistance(m_router->unreachableDistance);
    // process router packet related logic
    registerHandlerByMsgType(GatewayMessageType::RouterTableSyncSeq,
        [this](NetworkException exception, std::shared_ptr<P2PSession> session, Message message) {
            onReceiveRouterSeq(std::move(exception), std::move(session), message);
        });
    registerHandlerByMsgType(GatewayMessageType::RouterTableResponse,
        [this](NetworkException exception, std::shared_ptr<P2PSession> session, Message message) {
            onReceivePeersRouterTable(std::move(exception), std::move(session), message);
        });

    registerHandlerByMsgType(GatewayMessageType::RouterTableRequest,
        [this](NetworkException exception, std::shared_ptr<P2PSession> session, Message message) {
            onReceiveRouterTableRequest(std::move(exception), std::move(session), message);
        });
    registerOnNewSession([this](P2PSession::Ptr _session) { onNewSession(std::move(_session)); });
    registerOnDeleteSession(
        [this](P2PSession::Ptr _session) { onEraseSession(std::move(_session)); });

    // FIB-186 (vector B): the timer is the trailing-edge flush and the steady-state reconciliation.
    // It resets the coalescing flag so the next membership change is a fresh leading edge, and it
    // survives a broadcast failure by re-arming on the error path -- otherwise a single throwing
    // broadcast would leave the timer un-rearmed and router-seq sync would stop silently until
    // process restart (there is no other re-arm path once inline broadcasts are coalesced).
    m_router->routerTimer->registerTimeoutHandler([this]() {
        m_router->routerSeqDirty.store(false, std::memory_order_release);
        try
        {
            broadcastRouterSeq();
        }
        catch (std::exception const& e)
        {
            SERVICE2_LOG(WARNING) << LOG_BADGE("routerSeqSync")
                                  << LOG_DESC("broadcastRouterSeq exception")
                                  << LOG_KV("error", e.what());
            m_router->routerTimer->restart();
        }
    });
}

// receive routerTable from peers
void Service::onReceivePeersRouterTable(
    NetworkException _error, std::shared_ptr<P2PSession> _session, const Message& _message)
{
    if (_error.errorCode() != 0)
    {
        SERVICE2_LOG(WARNING) << LOG_BADGE("onReceivePeersRouterTable")
                              << LOG_KV("code", _error.errorCode()) << LOG_KV("msg", _error.what());
        return;
    }
    auto routerTable = m_router->routerTableFactory->createRouterTable(_message.payload());

    SERVICE2_LOG(INFO) << LOG_BADGE("onReceivePeersRouterTable")
                       << LOG_KV("peer", _session->printP2pID())
                       << LOG_KV("entrySize", routerTable->routerEntries().size());
    joinRouterTable(_session, routerTable);
}

void Service::joinRouterTable(
    std::shared_ptr<P2PSession> _session, std::shared_ptr<RouterTableInterface> _routerTable)
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
        if (m_router->routerTable->update(unreachableNodes, generatedFrom, entry) && !updated)
        {
            updated = true;
        }
        // update the nodeInfo
        updateP2pInfo(dstNodeInfo);
    }

    SERVICE2_LOG(INFO) << LOG_BADGE("joinRouterTable") << LOG_DESC("create router entry")
                       << LOG_KV("dst", printShortP2pID(generatedFrom));

    auto entry = m_router->routerTableFactory->createRouterEntry();
    entry->setDstNode(_session->p2pID());
    entry->setDstNodeInfo(_session->p2pInfo());
    entry->setDistance(0);
    if (m_router->routerTable->update(unreachableNodes, m_nodeID, entry) && !updated)
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
    m_router->statusSeq++;
    markRouterSeqChanged();
}

// receive routerTable request from peer
void Service::onReceiveRouterTableRequest(
    NetworkException _error, std::shared_ptr<P2PSession> _session, const Message& _message)
{
    if (_error.errorCode() != 0)
    {
        SERVICE2_LOG(WARNING) << LOG_BADGE("onReceiveRouterTableRequest")
                              << LOG_KV("code", _error.errorCode()) << LOG_KV("msg", _error.what());
        return;
    }
    SERVICE2_LOG(INFO) << LOG_BADGE("onReceiveRouterTableRequest")
                       << LOG_KV("peer", _session->printP2pID())
                       << LOG_KV("entrySize", m_router->routerTable->routerEntries().size());

    auto routerTableData = std::make_shared<bytes>();
    m_router->routerTable->encode(*routerTableData);
    auto dstP2PNodeID =
        (!_message.srcP2PNodeID().empty()) ? _message.srcP2PNodeID() : _session->p2pID();
    auto self = shared_from_this();
    // fire-and-forget through the coroutine fast path: the message is built in the frame and the
    // router table payload is moved into it (the caller's buffer does not outlive the deferred
    // send); an unreachable peer is an expected, recoverable state.
    task::wait([](std::shared_ptr<Service> _self, uint16_t _type, P2pID _nodeID,
                   bcos::bytes _payload) -> task::Task<void> {
        Message message;
        message.setPacketType(_type);
        message.setSeq(_self->newSeq());
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

void Service::broadcastRouterSeq()
{
    m_router->routerTimer->restart();
    auto seq = m_router->statusSeq.load();
    auto statusSeq = boost::asio::detail::socket_ops::host_to_network_long(seq);
    bytes payload;
    payload.insert(payload.end(), (byte*)&statusSeq, (byte*)&statusSeq + 4);
    auto self = shared_from_this();
    // value message held by shared_ptr; the 4-byte seq payload is owned by it (zero-copy view
    // send). The router table should only be exchanged between neighbours and propagated
    // hop-by-hop, so broadcast to the directly connected sessions only (not all reachable nodes).
    // All state is passed as coroutine parameters so it is copied into the frame and stays alive.
    task::wait([](std::shared_ptr<Service> _self, bcos::bytes _payload) mutable
                   -> task::Task<void> {
        auto message = std::make_shared<Message>();
        message->setPacketType(GatewayMessageType::RouterTableSyncSeq);
        message->setPayload(std::move(_payload));
        co_await _self->broadcastMessageToNeighbors(
            message, ::ranges::views::single(message->payload()), Options{});
    }(self, std::move(payload)));
}

void Service::markRouterSeqChanged()
{
    // FIB-186 (vector B): leading-edge coalesce. Broadcast the seq immediately on the first change
    // of a burst -- neighbours then re-request the current full router table, which already
    // reflects the changes made so far -- so a directly reachable peer converges without waiting on
    // the timer. Further churn within the window only marks the flag (no broadcast); the
    // routerTimer flushes the coalesced state once and resets the flag. The pre-fix code
    // broadcast on every single membership/route change, so connect/disconnect churn cascaded a
    // full-mesh seq->request->whole-table gossip on m_asyncGroup -- the pool that delivers PBFT
    // messages -- and starved consensus (CertiK vector B). broadcastRouterSeq() also restart()s the
    // timer, which resurrects it if a prior tick failed to re-arm.
    if (!m_router->routerSeqDirty.exchange(true, std::memory_order_acq_rel))
    {
        broadcastRouterSeq();
    }
}

void Service::onReceiveRouterSeq(
    NetworkException _error, std::shared_ptr<P2PSession> _session, const Message& _message)
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
    if (_message.payload().size() < sizeof(uint32_t))
    {
        SERVICE2_LOG(WARNING) << LOG_BADGE("onReceiveRouterSeq") << LOG_DESC("short payload, drop")
                              << LOG_KV("size", _message.payload().size());
        return;
    }
    uint32_t seq = 0;
    std::memcpy(&seq, _message.payload().data(), sizeof(seq));
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
        (!_message.srcP2PNodeID().empty()) ? _message.srcP2PNodeID() : _session->p2pID();
    auto self = shared_from_this();
    // fire-and-forget through the coroutine fast path: the message is built in the frame and the
    // (empty) payload rides as a view; an unreachable peer is an expected, recoverable state.
    task::wait([](std::shared_ptr<Service> _self, uint16_t _type, P2pID _nodeID)
                   -> task::Task<void> {
        Message message;
        message.setPacketType(_type);
        message.setSeq(_self->newSeq());
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

void Service::onNewSession(P2PSession::Ptr _session)
{
    // update the p2p information when establish new session
    updateP2pInfo(_session->p2pInfo());
    std::set<std::string> unreachableNodes;
    auto entry = m_router->routerTableFactory->createRouterEntry();
    entry->setDstNode(_session->p2pID());
    entry->setDstNodeInfo(_session->p2pInfo());
    entry->setDistance(0);
    if (!m_router->routerTable->update(unreachableNodes, m_nodeID, entry))
    {
        SERVICE2_LOG(INFO) << LOG_BADGE("onNewSession") << LOG_DESC("routerTable not changed")
                           << LOG_KV("dst", _session->printP2pID());
        return;
    }
    onP2PNodesUnreachable(unreachableNodes);
    m_router->statusSeq++;
    markRouterSeqChanged();
    SERVICE2_LOG(INFO) << LOG_BADGE("onNewSession") << LOG_DESC("update routerTable")
                       << LOG_KV("dst", _session->printP2pID());
}

void Service::onEraseSession(P2PSession::Ptr _session)
{
    eraseSeq(_session->p2pID());
    std::set<std::string> unreachableNodes;
    if (m_router->routerTable->erase(unreachableNodes, _session->p2pID()))
    {
        onP2PNodesUnreachable(unreachableNodes);
        m_router->statusSeq++;
        markRouterSeqChanged();
    }
    SERVICE2_LOG(INFO) << LOG_BADGE("onEraseSession") << LOG_KV("dst", _session->printP2pID());
}

bool Service::tryToUpdateSeq(std::string const& _p2pNodeID, uint32_t _seq)
{
    UpgradableGuard l(m_router->x_node2Seq);
    auto it = m_router->node2Seq.find(_p2pNodeID);
    if (it != m_router->node2Seq.end() && it->second >= _seq)
    {
        return false;
    }
    UpgradeGuard upgradeGuard(l);
    m_router->node2Seq[_p2pNodeID] = _seq;
    return true;
}

bool Service::eraseSeq(std::string const& _p2pNodeID)
{
    UpgradableGuard l(m_router->x_node2Seq);
    auto it = m_router->node2Seq.find(_p2pNodeID);
    if (it == m_router->node2Seq.end())
    {
        return false;
    }
    UpgradeGuard upgradeGuard(l);
    m_router->node2Seq.erase(it);
    return true;
}

std::string bcos::gateway::Service::getShortP2pID(std::string const& rawP2pID) const
{
    if (!m_router)
    {
        return rawP2pID;
    }
    if (rawP2pID.empty())
    {
        return rawP2pID;
    }
    // already the short p2pId
    if (!isRawP2pID(rawP2pID))
    {
        return rawP2pID;
    }
    bcos::ReadGuard lock(m_router->x_rawP2pIDInfo);
    if (auto it = m_router->rawP2pIDInfo.find(rawP2pID); it != m_router->rawP2pIDInfo.end())
    {
        return it->second;
    }
    // note: in the case of running old node with the new node, the shortP2pID maybe not found
    SERVICE2_LOG(TRACE) << LOG_DESC("getShortP2pID failed, return rawP2pID directly")
                        << LOG_KV("id", printShortP2pID(rawP2pID));
    return rawP2pID;
}

std::string bcos::gateway::Service::getRawP2pID(std::string const& shortP2pID) const
{
    if (!m_router)
    {
        return shortP2pID;
    }
    if (shortP2pID.empty())
    {
        return shortP2pID;
    }
    // the old node case
    if (isRawP2pID(shortP2pID))
    {
        return shortP2pID;
    }
    bcos::ReadGuard lock(m_router->x_p2pIDInfo);
    if (auto it = m_router->p2pIDInfo.find(shortP2pID); it != m_router->p2pIDInfo.end())
    {
        return it->second;
    }
    SERVICE2_LOG(WARNING) << LOG_DESC("getRawP2pID failed, return shortP2pID directly")
                          << LOG_KV("id", printShortP2pID(shortP2pID));
    return shortP2pID;
}

void bcos::gateway::Service::resetP2pID(
    Message& message, bcos::protocol::ProtocolVersion const& version)
{
    if (!m_router)
    {
        return;
    }
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

void bcos::gateway::Service::onP2PNodesUnreachable(std::set<std::string> const& _p2pNodeIDs)
{
    std::vector<std::function<void(std::string)>> handlers;
    {
        ReadGuard readGuard(m_router->x_unreachableHandlers);
        handlers = m_router->unreachableHandlers;
    }
    // TODO: async here
    for (auto const& node : _p2pNodeIDs)
    {
        for (auto const& it : m_router->unreachableHandlers)
        {
            it(node);
        }
    }
}

void bcos::gateway::Service::updateP2pInfo(P2PInfo const& p2pInfo)
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

void bcos::gateway::Service::tryToUpdateRawP2pInfo(P2PInfo const& p2pInfo)
{
    bcos::UpgradableGuard lock(m_router->x_rawP2pIDInfo);
    auto it = m_router->rawP2pIDInfo.find(p2pInfo.rawP2pID);
    if (it != m_router->rawP2pIDInfo.end())
    {
        return;
    }
    m_router->rawP2pIDInfo.insert(std::make_pair(p2pInfo.rawP2pID, p2pInfo.p2pID));
}

void bcos::gateway::Service::tryToUpdateP2pInfo(P2PInfo const& p2pInfo)
{
    bcos::UpgradableGuard lock(m_router->x_p2pIDInfo);
    auto it = m_router->p2pIDInfo.find(p2pInfo.p2pID);
    if (it != m_router->p2pIDInfo.end())
    {
        return;
    }
    m_router->p2pIDInfo.insert(std::make_pair(p2pInfo.p2pID, p2pInfo.rawP2pID));
}
