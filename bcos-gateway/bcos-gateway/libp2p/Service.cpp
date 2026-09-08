/** @file Service.cpp
 *  @author chaychen
 *  @date 20180910
 */

#include "bcos-gateway/libp2p/Service.h"
#include "bcos-framework/Common.h"
#include "bcos-framework/protocol/GlobalConfig.h"
#include "bcos-gateway/libnetwork/Common.h"
#include "bcos-gateway/libnetwork/Session.h"
#include "bcos-gateway/libnetwork/Socket.h"
#include "bcos-gateway/libp2p/Common.h"
#include "bcos-gateway/libp2p/P2PMessage.h"
#include "bcos-gateway/libp2p/P2PMessageV2.h"
#include "bcos-gateway/libp2p/P2PSession.h"  // for P2PSession
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/Common.h"
#include <bcos-task/Wait.h>
#include <boost/random.hpp>
#include <boost/throw_exception.hpp>
#include <range/v3/view/map.hpp>
#include <range/v3/view/transform.hpp>
#include <cstring>
#include <shared_mutex>
#include <utility>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::protocol;

static const uint32_t CHECK_INTERVAL = 10000;

static bool isRawP2pID(std::string const& p2pID)
{
    return p2pID.size() > HASH_NODEID_MAX_SIZE;
}

Service::Service(P2PInfo const& _p2pInfo) : m_selfInfo(_p2pInfo), m_nodeID(m_selfInfo.rawP2pID)
{
    m_msgHandlers.fill(nullptr);
    m_localProtocol = g_BCOSConfig.protocolInfo(ProtocolModuleID::GatewayService);

    SERVICE_LOG(INFO) << LOG_BADGE("Service::Service") << LOG_DESC("local protocol")
                      << LOG_KV("protocolModuleID", m_localProtocol->protocolModuleID())
                      << LOG_KV("version", m_localProtocol->version())
                      << LOG_KV("minVersion", m_localProtocol->minVersion())
                      << LOG_KV("maxVersion", m_localProtocol->maxVersion());

    m_codec = g_BCOSConfig.codec();
    // Process handshake packet logic, handshake protocol and determine
    // the version, when handshake finished the version field of P2PMessage
    // should be set
    registerHandlerByMsgType(GatewayMessageType::Handshake,
        [this](NetworkException exception, std::shared_ptr<P2PSession> session,
            P2PMessage::Ptr message) {
            onReceiveProtocol(std::move(exception), std::move(session), std::move(message));
        });

    registerHandlerByMsgType(GatewayMessageType::Heartbeat,
        [this](NetworkException exception, std::shared_ptr<P2PSession> session,
            P2PMessage::Ptr message) {
            onReceiveHeartbeat(std::move(exception), std::move(session), std::move(message));
        });
}

Service::Service(P2PInfo const& _p2pInfo, boost::asio::io_context& _ioContext) : Service(_p2pInfo)
{
    m_enableRIPProtocol = true;
    m_routerTimer = std::make_shared<Timer>(_ioContext, 3000, "routerSeqSync");
    m_routerTable = std::make_shared<RouterTable>();

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

Service::~Service()
{
    stop();
}

void Service::start()
{
    if (!m_run)
    {
        m_run = true;

        auto self = std::weak_ptr<Service>(shared_from_this());
        m_host->setConnectionHandler([self](NetworkException e, P2PInfo const& p2pInfo,
                                         std::shared_ptr<Session> session) {
            auto service = self.lock();
            if (service)
            {
                service->onConnect(std::move(e), p2pInfo, std::move(session));
            }
        });
        m_host->start();

        heartBeat();

        if (m_routerTimer)
        {
            m_routerTimer->start();
        }
    }
}

void Service::stop()
{
    if (m_routerTimer)
    {
        m_routerTimer->stop();
    }
    if (m_run)
    {
        m_run = false;
        if (m_timer)
        {
            m_timer->cancel();
        }
        m_host->stop();

        /// disconnect sessions
        std::unique_lock lock(x_sessions);
        for (auto& session : m_sessions)
        {
            session.second->stop(ClientQuit);
        }

        /// clear sessions
        m_sessions.clear();
    }
}

void Service::heartBeat()
{
    if (!m_run)
    {
        return;
    }

    // FIB-186 (vector D): snapshot the static-node list under x_nodes and release it BEFORE calling
    // isConnected()/asyncConnect(), both of which take x_sessions. Holding x_nodes across an
    // x_sessions acquisition here is the reverse of the order onConnect uses (x_sessions ->
    // x_nodes, via updateStaticNodes), which deadlocks under connection churn: onConnect holds
    // x_sessions(W) waiting for x_nodes(W) while heartBeat holds x_nodes(R) waiting for
    // x_sessions(R). Copying the list then dropping x_nodes makes every path acquire x_sessions
    // before x_nodes.
    std::vector<std::pair<NodeIPEndpoint, P2pID>> staticNodes;
    {
        std::shared_lock nodeLock(x_nodes);
        staticNodes.assign(m_staticNodes.begin(), m_staticNodes.end());
    }

    // Reconnect all nodes
    for (auto& it : staticNodes)
    {
        /// exclude myself
        if (it.second == id())
        {
            continue;
        }
        if (!it.second.empty() && isConnected(it.second))
        {
            SERVICE_LOG(TRACE) << LOG_DESC("heartBeat ignore connected")
                               << LOG_KV("endpoint", it.first)
                               << LOG_KV("nodeid", printShortP2pID(it.second));
            continue;
        }
        SERVICE_LOG(DEBUG) << LOG_DESC("heartBeat try to reconnect")
                           << LOG_KV("endpoint", it.first);
        m_host->asyncConnect(it.first,
            [service = shared_from_this()](auto error, const P2PInfo& p2pInfo, auto session) {
                service->onConnect(std::move(error), p2pInfo, std::move(session));
            });
    }

    std::shared_lock sessionLock(x_sessions);
    SERVICE_LOG(INFO) << METRIC << LOG_DESC("heartBeat")
                      << LOG_KV("connected count", m_sessions.size());
    for (auto const& it : m_sessions)
    {
        auto session = it.second;
        auto queueSize = session->session()->writeQueueSize();
        if (queueSize > 0)
        {
            SERVICE_LOG(INFO) << METRIC << LOG_DESC("heartBeat")
                              << LOG_KV("endpoint", session->session()->nodeIPEndpoint())
                              << LOG_KV("write queue size", queueSize);
        }
        else
        {
            SERVICE_LOG(DEBUG) << METRIC << LOG_DESC("heartBeat")
                               << LOG_KV("endpoint", session->session()->nodeIPEndpoint())
                               << LOG_KV("write queue size", queueSize);
        }
    }
    sessionLock.unlock();

    auto self = std::weak_ptr<Service>(shared_from_this());
    m_timer.emplace(m_host->asioInterface()->newTimer(CHECK_INTERVAL));
    m_timer->async_wait([self](const boost::system::error_code& error) {
        if (error)
        {
            SERVICE_LOG(WARNING) << "timer canceled" << LOG_KV("code", error);
            return;
        }
        auto service = self.lock();
        if (service && service->host()->haveNetwork())
        {
            service->heartBeat();
        }
    });
}

/// update the staticNodes
void Service::updateStaticNodes(std::shared_ptr<Socket> const& _s, P2pID const& nodeID)
{
    NodeIPEndpoint endpoint(_s->nodeIPEndpoint());
    std::unique_lock nodeLock(x_nodes);
    auto it = m_staticNodes.find(endpoint);
    // modify m_staticNodes(including accept cases, namely the client endpoint)
    if (it != m_staticNodes.end())
    {
        SERVICE_LOG(INFO) << LOG_DESC("updateStaticNodes")
                          << LOG_KV("nodeid", printShortP2pID(nodeID))
                          << LOG_KV("endpoint", endpoint);
        it->second = nodeID;
    }
    else
    {
        SERVICE_LOG(DEBUG) << LOG_DESC("updateStaticNodes can't find endpoint")
                           << LOG_KV("nodeid", printShortP2pID(nodeID))
                           << LOG_KV("endpoint", endpoint);
    }
}

void Service::onConnect(
    NetworkException e, P2PInfo const& p2pInfo, std::shared_ptr<Session> session)
{
    P2pID p2pID = p2pInfo.rawP2pID;
    std::string peer = "unknown";
    if (session)
    {
        peer = session->nodeIPEndpoint().address() + ":" +
               std::to_string(session->nodeIPEndpoint().port());
    }
    if (e.errorCode())
    {
        SERVICE_LOG(WARNING) << LOG_DESC("onConnect") << LOG_KV("code", e.errorCode())
                             << LOG_KV("p2pid", printShortP2pID(p2pID))
                             << LOG_KV("nodeName", p2pInfo.nodeName) << LOG_KV("endpoint", peer)
                             << LOG_KV("message", e.what());

        return;
    }

    SERVICE_LOG(INFO) << LOG_DESC("onConnect") << LOG_KV("p2pid", printShortP2pID(p2pID))
                      << LOG_KV("endpoint", peer);

    if (p2pID == id())
    {
        SERVICE_LOG(TRACE) << "Disconnect self";
        updateStaticNodes(session->socket(), id());
        session->disconnect(DuplicatePeer);
        return;
    }

    auto p2pSession = std::make_shared<P2PSession>();
    p2pSession->setSession(session);
    p2pSession->setP2PInfo(p2pInfo);
    p2pSession->setService(weak_from_this());
    p2pSession->setProtocolInfo(m_localProtocol);

    auto p2pSessionWeakPtr = std::weak_ptr<P2PSession>(p2pSession);
    p2pSession->session()->setMessageHandler([self = shared_from_this(), p2pSessionWeakPtr](
                                                 auto&& exception, auto&& session, auto&& message) {
        self->onMessage(std::forward<decltype(exception)>(exception),
            std::forward<decltype(session)>(session), std::forward<decltype(message)>(message),
            p2pSessionWeakPtr);
    });
    p2pSession->session()->setBeforeMessageHandler(
        [this](Session& session, const Message& message, uint32_t wireLength) {
            return onBeforeMessage(session, message, wireLength);
        });

    // Note: the lock must be here, otherwise there will be more than one started sessions,
    // and a session not maintained in m_sessions will be choosed when send messages in some cases
    // which will cause coredump
    std::unique_lock lock(x_sessions);
    auto existedSession = getP2PSessionByNodeIdWithoutLock(p2pID);
    if (existedSession && existedSession->active())
    {
        SERVICE_LOG(INFO) << "Disconnect duplicate peer" << LOG_KV("p2pid", printShortP2pID(p2pID))
                          << LOG_KV("endpoint", peer);
        updateStaticNodes(session->socket(), p2pID);
        session->disconnect(DuplicatePeer);
        return;
    }
    p2pSession->start();
    asyncSendProtocol(p2pSession);
    updateStaticNodes(session->socket(), p2pID);

    if (existedSession)
    {
        m_sessions[p2pID] = p2pSession;
        lock.unlock();
    }
    else
    {
        m_sessions.insert(std::make_pair(p2pID, p2pSession));
        lock.unlock();
        callNewSessionHandlers(p2pSession);
    }
    SERVICE_LOG(INFO) << LOG_DESC("Connection established")
                      << LOG_KV("p2pid", printShortP2pID(p2pID))
                      << LOG_KV("shortP2pid", printShortP2pID(p2pInfo.p2pID))
                      << LOG_KV("endpoint", session->nodeIPEndpoint());
}

void Service::onDisconnect(NetworkException e, P2PSession::Ptr p2pSession)
{
    // handle all registered handlers
    for (const auto& handler : m_disconnectionHandlers)
    {
        handler(e, p2pSession);
    }
    auto session = getP2PSessionByNodeId(p2pSession->p2pID());
    if (session && session == p2pSession)
    {
        SERVICE_LOG(TRACE) << "Service onDisconnect and remove from sessions"
                           << LOG_KV("p2pid", p2pSession->printP2pID())
                           << LOG_KV("endpoint", p2pSession->session()->nodeIPEndpoint());
        {
            std::unique_lock l(x_sessions);
            m_sessions.erase(p2pSession->p2pID());
        }
        callDeleteSessionHandlers(p2pSession);

        if (e.errorCode() == P2PExceptionType::DuplicateSession)
        {
            return;
        }
        SERVICE_LOG(INFO) << LOG_DESC("onDisconnect") << LOG_KV("code", e.errorCode())
                          << LOG_KV("what", boost::diagnostic_information(e));
        std::unique_lock nodeLock(x_nodes);
        for (auto& it : m_staticNodes)
        {
            if (it.second == p2pSession->p2pID())
            {
                it.second.clear();  // clear nodeid info when disconnect
                break;
            }
        }
    }
    // heartBeat();
}

void Service::sendRespMessageBySession(
    bytesConstRef _payload, P2PMessage::Ptr _p2pMessage, P2PSession::Ptr _p2pSession)
{
    if (m_enableRIPProtocol &&
        _p2pSession->protocolInfo()->version() > bcos::protocol::ProtocolVersion::V0)
    {
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
        return;
    }

    auto self = shared_from_this();
    auto seq = _p2pMessage->seq();
    auto p2pid = _p2pSession->p2pID();
    // value message in frame; the (borrowed) response payload is copied into the frame because the
    // receive callback that passed it does not outlive the deferred send. The session/service are
    // passed as coroutine parameters so they are copied into the frame.
    task::wait([](std::shared_ptr<Service> _self, P2PSession::Ptr _p2pSession,
                   bcos::bytes _payload, uint32_t _seq, P2pID _p2pid) -> task::Task<void> {
        try
        {
            P2PMessageV2 respMessage;
            respMessage.setSeq(_seq);
            respMessage.setRespPacket();
            respMessage.setPayload(std::move(_payload));
            co_await _p2pSession->fastSendP2PMessage(
                respMessage, ::ranges::views::single(respMessage.payload()), Options{});
            if (c_fileLogLevel <= TRACE) [[unlikely]]
            {
                SERVICE_LOG(TRACE) << "sendRespMessageBySession" << LOG_KV("seq", _seq)
                                   << LOG_KV("p2pid", printShortP2pID(_p2pid))
                                   << LOG_KV("payload size", respMessage.payload().size());
            }
        }
        catch (std::exception const& e)
        {
            // Same shape as the RIP-mode sendRespMessageBySession (which delegates V0 peers
            // here): a synchronous pre-send rejection on the response path must not propagate out
            // of the receiving handler.
            SERVICE_LOG(WARNING) << LOG_BADGE("sendRespMessageBySession")
                                 << LOG_DESC("send response failed") << LOG_KV("seq", _seq)
                                 << LOG_KV("p2pid", printShortP2pID(_p2pid))
                                 << LOG_KV("what", boost::diagnostic_information(e));
        }
    }(self, _p2pSession, bcos::bytes(_payload.begin(), _payload.end()), seq, p2pid));
}

std::optional<bcos::Error> Service::onBeforeMessage(
    Session& _session, const Message& _message, uint32_t _wireLength)
{
    if (m_beforeMessageHandler)
    {
        return m_beforeMessageHandler(_session, _message, _wireLength);
    }

    return std::nullopt;
}

void Service::onMessage(NetworkException e, Session::Ptr session, Message::Ptr message,
    std::weak_ptr<P2PSession> p2pSessionWeakPtr)
{
    if (m_enableRIPProtocol)
    {
        if (e.errorCode() != 0)
        {
            SERVICE2_LOG(WARNING) << LOG_BADGE("onMessage") << LOG_KV("code", e.errorCode())
                                  << LOG_KV("msg", e.what());
            // falls through to the base dispatch to trigger disconnectHandler
        }
        else
        {
            // v0 message or the dstP2PNodeID is the nodeSelf or empty
            auto p2pMsg = std::dynamic_pointer_cast<P2PMessageV2>(message);
            auto dstNodeP2pID = getRawP2pID(p2pMsg->dstP2PNodeID());
            if (!p2pMsg->dstP2PNodeID().empty() && dstNodeP2pID != m_nodeID)
            {
                // forward the message again
                auto ttl = (int16_t)p2pMsg->ttl();
                if (ttl <= 0)
                {
                    SERVICE2_LOG(WARNING)
                        << LOG_BADGE("onMessage") << LOG_DESC("expired ttl")
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
                    SERVICE2_LOG(TRACE)
                        << LOG_BADGE("onMessage") << LOG_DESC("forwardMessage")
                        << LOG_KV("seq", p2pMsg->seq())
                        << LOG_KV("from", p2pMsg->printSrcP2PNodeID())
                        << LOG_KV("dst", p2pMsg->printDstP2PNodeID())
                        << LOG_KV("type", p2pMsg->packetType()) << LOG_KV("seq", p2pMsg->seq())
                        << LOG_KV("rsp", p2pMsg->isRespPacket()) << LOG_KV("ttl", p2pMsg->ttl())
                        << LOG_KV("payLoadSize", p2pMsg->payload().size());
                }
                // forward through the coroutine fast path (zero-copy: the received message is
                // passed as a coroutine parameter so it is copied into the frame and stays alive,
                // and its payload is sent as a view). Note: forwarding must NOT rewrite
                // srcP2PNodeID (that is only done when this node originates the message) —
                // forwardMessageByNodeID only resolves the next hop.
                auto self = shared_from_this();
                task::wait([](std::shared_ptr<Service> _self,
                               std::shared_ptr<P2PMessageV2> _p2pMsg) -> task::Task<void> {
                    try
                    {
                        co_await _self->forwardMessageByNodeID(_p2pMsg->dstP2PNodeID(), *_p2pMsg,
                            ::ranges::views::single(_p2pMsg->payload()), Options{});
                    }
                    catch (std::exception const& e)
                    {
                        // A synchronous pre-send rejection (rate limit / max size) or an async
                        // write failure on the relay path is expected during bandwidth saturation —
                        // log the next hop so a "peer not receiving relayed messages"
                        // investigation keeps the routing dimension.
                        SERVICE2_LOG(WARNING)
                            << LOG_BADGE("onMessage") << LOG_DESC("forwardMessage failed")
                            << LOG_KV("dst", _p2pMsg->dstP2PNodeID())
                            << LOG_KV("seq", _p2pMsg->seq())
                            << LOG_KV("what", boost::diagnostic_information(e));
                    }
                }(self, p2pMsg));
                return;
            }
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
        }
    }

    auto p2pSession = p2pSessionWeakPtr.lock();
    if (!p2pSession)
    {
        return;
    }

    try
    {
        P2pID p2pID = id();
        NodeIPEndpoint nodeIPEndpoint(boost::asio::ip::address(), 0);
        if (session && p2pSession)
        {
            p2pID = p2pSession->p2pID();
            nodeIPEndpoint = session->nodeIPEndpoint();
        }

        if (e.errorCode())
        {
            SERVICE_LOG(INFO) << LOG_DESC("disconnect failed in P2PSession")
                              << LOG_KV("p2pid", printShortP2pID(p2pID))
                              << LOG_KV("endpoint", nodeIPEndpoint) << LOG_KV("code", e.errorCode())
                              << LOG_KV("message", e.what());

            if (p2pSession)
            {
                p2pSession->stop(UserReason);
                onDisconnect(e, p2pSession);
            }
            return;
        }

        if (auto result =
                (m_onMessageHandler ? m_onMessageHandler(session, message) : std::nullopt))
        {
            auto& error = result.value();
            // TODO:  For p2p basic message type, direct discard request ???
            SERVICE_LOG(TRACE) << LOG_DESC("onMessage receive message")
                               << LOG_DESC(error.errorMessage())
                               << LOG_KV("endpoint", nodeIPEndpoint)
                               << LOG_KV("seq", message->seq())
                               << LOG_KV("version", message->version())
                               << LOG_KV("packetType", message->packetType());
            return;
        }

        /// SERVICE_LOG(TRACE) << "Service onMessage: " << message->seq();
        auto p2pMessage = std::dynamic_pointer_cast<P2PMessage>(message);
        if (c_fileLogLevel <= TRACE) [[unlikely]]
        {
            SERVICE_LOG(TRACE) << LOG_DESC("onMessage receive message")
                               << LOG_KV("p2pid", printShortP2pID(p2pID))
                               << LOG_KV("endpoint", nodeIPEndpoint)
                               << LOG_KV("seq", p2pMessage->seq())
                               << LOG_KV("version", p2pMessage->version())
                               << LOG_KV("packetType", p2pMessage->packetType());
        }

        auto packetType = p2pMessage->packetType();
        auto ext = p2pMessage->ext();
        auto version = p2pMessage->version();
        auto handler = getMessageHandlerByMsgType(packetType);
        if (handler)
        {
            handler(e, p2pSession, p2pMessage);
            return;
        }

        if (message->packetType() == gateway::AMOPMessageType)
        {
            // AMOP May be disable by config.ini
            SERVICE_LOG(DEBUG) << LOG_DESC("Unrecognized message type")
                               << LOG_DESC(": AMOP is disabled!") << LOG_KV("seq", message->seq())
                               << LOG_KV("packetType", packetType) << LOG_KV("ext", ext)
                               << LOG_KV("version", version)
                               << LOG_KV("dst p2p", p2pMessage->printDstP2PNodeID());
            return;
        }
        SERVICE_LOG(ERROR) << LOG_DESC("Unrecognized message type") << LOG_KV("seq", message->seq())
                           << LOG_KV("packetType", packetType) << LOG_KV("ext", ext)
                           << LOG_KV("version", version)
                           << LOG_KV("dstp2p", p2pMessage->printDstP2PNodeID());
    }
    catch (std::exception& e)
    {
        SERVICE_LOG(ERROR) << "onMessage error" << LOG_KV("what", boost::diagnostic_information(e));
    }
}

bcos::task::Task<void> Service::broadcastMessageToAll(P2PMessage::Ptr message,
    ::ranges::any_view<bytesConstRef, ::ranges::category::forward> payloads, Options options)
{
    std::vector<P2pID> nodeIDs;
    if (m_enableRIPProtocol)
    {
        // broadcast to all reachable nodes through the router table
        auto reachableNodes = m_routerTable->getAllReachableNode();
        nodeIDs.assign(reachableNodes.begin(), reachableNodes.end());
    }
    else
    {
        std::shared_lock lock(x_sessions);
        nodeIDs.reserve(m_sessions.size());
        for (auto const& session : m_sessions)
        {
            nodeIDs.push_back(session.first);
        }
    }
    auto self = shared_from_this();
    // Fan out one independent coroutine per peer (see broadcastMessageToNeighbors): the caller's
    // message is handed over as a shared_ptr so every per-peer task keeps it alive; each task's
    // per-session src/dst/version stamping runs synchronously before its first suspension, so no
    // race on the shared header and no head-of-line blocking on a stalled peer's socket.
    for (auto const& nodeID : nodeIDs)
    {
        task::wait([](std::shared_ptr<Service> _self, P2pID _nodeID, P2PMessage::Ptr _message,
                       ::ranges::any_view<bytesConstRef, ::ranges::category::forward> _payloads,
                       Options _options) mutable -> task::Task<void> {
            try
            {
                co_await _self->sendMessageByNodeID(
                    _nodeID, *_message, std::move(_payloads), std::move(_options));
            }
            catch (std::exception const& e)
            {
                SERVICE_LOG(WARNING) << LOG_DESC("broadcastMessageToAll failed")
                                     << LOG_KV("nodeid", printShortP2pID(_nodeID))
                                     << LOG_KV("what", boost::diagnostic_information(e));
            }
        }(self, nodeID, message, payloads, options));
    }
    co_return;
}

bcos::task::Task<void> Service::broadcastMessageToNeighbors(P2PMessage::Ptr message,
    ::ranges::any_view<bytesConstRef, ::ranges::category::forward> payloads, Options options)
{
    // Only directly connected sessions (m_sessions), unlike broadcastMessageToAll which may fan out
    // through the RIP router table. This preserves the "router table sync only between
    // neighbors, propagated hop-by-hop" gossip model used by broadcastRouterSeq.
    std::vector<P2pID> nodeIDs;
    {
        std::shared_lock lock(x_sessions);
        nodeIDs.reserve(m_sessions.size());
        for (auto const& session : m_sessions)
        {
            nodeIDs.push_back(session.first);
        }
    }
    auto self = shared_from_this();
    // Fan out one independent coroutine per peer: the caller's message
    // is handed over as a shared_ptr so every per-peer task keeps it alive; each task's
    // per-session src/dst/version stamping runs synchronously before its first suspension, so no
    // race on the shared header and no head-of-line blocking on a stalled peer's socket.
    for (auto const& nodeID : nodeIDs)
    {
        task::wait([](std::shared_ptr<Service> _self, P2pID _nodeID, P2PMessage::Ptr _message,
                       ::ranges::any_view<bytesConstRef, ::ranges::category::forward> _payloads,
                       Options _options) mutable -> task::Task<void> {
            try
            {
                co_await _self->sendMessageByNodeID(
                    _nodeID, *_message, std::move(_payloads), std::move(_options));
            }
            catch (std::exception const& e)
            {
                SERVICE_LOG(WARNING) << LOG_DESC("broadcastMessageToNeighbors failed")
                                     << LOG_KV("nodeid", printShortP2pID(_nodeID))
                                     << LOG_KV("what", boost::diagnostic_information(e));
            }
        }(self, nodeID, message, payloads, options));
    }
    co_return;
}

P2PInfos Service::sessionInfos()
{
    std::shared_lock lock(x_sessions);
    return ::ranges::views::values(m_sessions) |
           ::ranges::views::transform([](auto const& session) { return session->p2pInfo(); }) |
           ::ranges::to<P2PInfos>();
}

bool Service::isConnected(P2pID const& nodeID) const
{
    auto session = getP2PSessionByNodeId(nodeID);
    return session && session->active();
}

bcos::task::Task<void> Service::sendMessageByNodeIDs(uint16_t _type,
    const std::vector<P2pID>& _nodeIDs, bcos::bytes _payload, Options _options)
{
    // value message held by shared_ptr: fan out one independent coroutine per node, so a stalled
    // peer's socket write cannot delay delivery to the peers behind it (same head-of-line-blocking
    // reasoning as broadcastMessageToAll). Each per-peer task keeps the message alive; its
    // per-session src/dst/version stamping runs synchronously before its first suspension, so the
    // shared header stays race-free. A failed/unreachable node is logged and skipped.
    auto message = std::make_shared<P2PMessageV2>();
    message->setPacketType(_type);
    message->setSeq(m_messageFactory->newSeq());
    message->setPayload(std::move(_payload));
    auto self = shared_from_this();
    for (auto const& nodeID : _nodeIDs)
    {
        task::wait([](std::shared_ptr<Service> _self, P2pID _nodeID, P2PMessage::Ptr _message,
                       Options _options) -> task::Task<void> {
            try
            {
                co_await _self->sendMessageByNodeID(_nodeID, *_message,
                    ::ranges::views::single(_message->payload()),
                    Options{_options.timeout, false});
            }
            catch (NetworkException const& e)
            {
                SERVICE_LOG(INFO) << LOG_DESC("sendMessageByNodeIDs send failed")
                                  << LOG_KV("nodeid", printShortP2pID(_nodeID))
                                  << LOG_KV("code", e.errorCode()) << LOG_KV("message", e.what());
            }
            catch (std::exception const& e)
            {
                // a non-network throw must not escape this detached fan-out task onto the io
                // thread that resumed it: log and skip the node, as for a send failure
                SERVICE_LOG(WARNING)
                    << LOG_DESC("sendMessageByNodeIDs unexpected exception, skip node")
                    << LOG_KV("nodeid", printShortP2pID(_nodeID))
                    << LOG_KV("message", boost::diagnostic_information(e));
            }
        }(self, nodeID, message, _options));
    }
    co_return;
}

// send the protocolInfo
void Service::asyncSendProtocol(P2PSession::Ptr _session)
{
    auto self = shared_from_this();
    // value message in frame; payload owned by the frame. All state is passed as coroutine
    // parameters so it is copied into the frame and stays alive. The pre-send checks (outgoing
    // rate limit / max size) run synchronously on the caller thread inside fastSendP2PMessage and
    // may throw — catch so a handshake rejection cannot escape task::wait and abort onConnect's
    // session-registration tail (the session would stay live-but-unregistered, silently hidden
    // from the routing layer). A failed handshake is recoverable: the peer retries.
    task::wait([](std::shared_ptr<Service> _self, P2PSession::Ptr _session) -> task::Task<void> {
        try
        {
            auto payload = bytes();
            _self->m_codec->encode(_self->m_localProtocol, payload);
            P2PMessageV2 message;
            message.setPacketType(GatewayMessageType::Handshake);
            message.setSeq(_self->messageFactory()->newSeq());
            message.setPayload(std::move(payload));
            SERVICE_LOG(INFO) << LOG_DESC("asyncSendProtocol")
                              << LOG_KV("payload", message.payload().size())
                              << LOG_KV("seq", message.seq());
            co_await _session->fastSendP2PMessage(
                message, ::ranges::views::single(message.payload()), Options{});
        }
        catch (std::exception const& e)
        {
            SERVICE_LOG(WARNING) << LOG_DESC("asyncSendProtocol send exception")
                                 << LOG_KV("p2pid", printShortP2pID(_session->p2pID()))
                                 << LOG_KV("what", boost::diagnostic_information(e));
        }
    }(self, _session));
}

// receive the heartbeat msg
void Service::onReceiveHeartbeat(
    NetworkException /*unused*/, std::shared_ptr<P2PSession> _session, P2PMessage::Ptr /*unused*/)
{
    std::string endpoint = "unknown";
    if (_session)
    {
        endpoint = _session->session()->nodeIPEndpoint().address();
    }

    SERVICE_LOG(TRACE) << LOG_BADGE("onReceiveHeartbeat") << LOG_DESC("receive heartbeat message")
                       << LOG_KV("endpoint", endpoint);
}

// receive the protocolInfo
void Service::onReceiveProtocol(
    NetworkException _error, std::shared_ptr<P2PSession> _session, P2PMessage::Ptr _message)
{
    if (_error.errorCode())
    {
        SERVICE_LOG(WARNING) << LOG_DESC("onReceiveProtocol failed")
                             << LOG_KV("code", _error.errorCode()) << LOG_KV("msg", _error.what())
                             << LOG_KV("peer", _session ? _session->printP2pID() : "unknown");
        return;
    }
    try
    {
        auto payload = _message->payload();
        auto protocolInfo = m_codec->decode(bytesConstRef(payload.data(), payload.size()));
        // negotiated version
        if (protocolInfo->minVersion() > m_localProtocol->maxVersion() ||
            protocolInfo->maxVersion() < m_localProtocol->minVersion())
        {
            SERVICE_LOG(WARNING)
                << LOG_DESC("onReceiveProtocol: protocolNegotiate failed, disconnect the session")
                << LOG_KV("peer", _session->printP2pID())
                << LOG_KV("minVersion", protocolInfo->minVersion())
                << LOG_KV("maxVersion", protocolInfo->maxVersion())
                << LOG_KV("supportMinVersion", m_localProtocol->minVersion())
                << LOG_KV("supportMaxVersion", m_localProtocol->maxVersion());
            _session->session()->disconnect(DisconnectReason::NegotiateFailed);
            return;
        }
        auto version = std::min(m_localProtocol->maxVersion(), protocolInfo->maxVersion());
        protocolInfo->setVersion(version);
        _session->setProtocolInfo(protocolInfo);
        SERVICE_LOG(INFO) << LOG_DESC("onReceiveProtocol: protocolNegotiate success")
                          << LOG_KV("peer", _session->printP2pID())
                          << LOG_KV("minVersion", protocolInfo->minVersion())
                          << LOG_KV("maxVersion", protocolInfo->maxVersion())
                          << LOG_KV("supportMinVersion", m_localProtocol->minVersion())
                          << LOG_KV("supportMaxVersion", m_localProtocol->maxVersion())
                          << LOG_KV("negotiatedVersion", version);
    }
    catch (std::exception const& e)
    {
        SERVICE_LOG(WARNING) << LOG_DESC("onReceiveProtocol exception")
                             << LOG_KV("peer", _session ? _session->printP2pID() : "unknown")
                             << LOG_KV("packetType", _message->packetType())
                             << LOG_KV("seq", _message->seq());
    }
}

void Service::updatePeerBlacklist(const std::set<std::string>& _strList, const bool _enable)
{
    // update the config
    m_host->peerBlacklist()->update(_strList, _enable);
    // disconnect nodes in the blacklist
    if (_enable)
    {
        std::shared_lock lock(x_sessions);
        for (const auto& session : m_sessions)
        {
            auto p2pIdWithoutExtInfo = session.second->p2pInfo().p2pIDWithoutExtInfo;
            if (_strList.end() == _strList.find(p2pIdWithoutExtInfo))
            {
                continue;
            }

            SERVICE_LOG(INFO) << LOG_DESC("updatePeerBlacklist, disconnect peer in blacklist")
                              << LOG_KV("peer", p2pIdWithoutExtInfo);

            updateStaticNodes(session.second->session()->socket(), session.second->p2pID());
            session.second->session()->disconnect(DisconnectReason::InBlacklistReason);
        }
    }
}

void Service::updatePeerWhitelist(const std::set<std::string>& _strList, const bool _enable)
{
    // update the config
    m_host->peerWhitelist()->update(_strList, _enable);
    // disconnect nodes not in the whitelist
    if (_enable)
    {
        std::shared_lock lock(x_sessions);
        for (const auto& session : m_sessions)
        {
            auto p2pIdWithoutExtInfo = session.second->p2pInfo().p2pIDWithoutExtInfo;
            if (_strList.end() != _strList.find(p2pIdWithoutExtInfo))
            {
                continue;
            }

            SERVICE_LOG(INFO) << LOG_DESC("updatePeerWhitelist, disconnect peer not in whitelist")
                              << LOG_KV("peer", p2pIdWithoutExtInfo);

            updateStaticNodes(session.second->session()->socket(), session.second->p2pID());
            session.second->session()->disconnect(DisconnectReason::NotInWhitelistReason);
        }
    }
}

bcos::task::Task<Message::Ptr> bcos::gateway::Service::sendMessageByNodeID(
    P2pID nodeID, P2PMessage& header, ::ranges::any_view<bytesConstRef> payloads, Options options)
{
    if (m_enableRIPProtocol)
    {
        // this node originates the message: stamp src/dst before routing
        header.setSrcP2PNodeID(m_nodeID);
        header.setDstP2PNodeID(nodeID);

        co_return co_await forwardMessageByNodeID(nodeID, header, std::move(payloads), options);
    }
    co_return co_await directSendMessageByNodeID(std::move(nodeID), header, std::move(payloads),
        options);
}

bcos::task::Task<Message::Ptr> bcos::gateway::Service::directSendMessageByNodeID(
    P2pID nodeID, P2PMessage& header, ::ranges::any_view<bytesConstRef> payloads, Options options)
{
    if (nodeID == id())
    {
        co_return {};
    }

    auto session = getP2PSessionByNodeId(nodeID);
    if (!session || !session->active())
    {
        BOOST_THROW_EXCEPTION(
            NetworkException(-1, "send message failed for no network established"));
    }
    if (header.seq() == 0)
    {
        header.setSeq(m_messageFactory->newSeq());
    }

    co_return co_await session->fastSendP2PMessage(header, std::move(payloads), options);
}

bcos::task::Task<Message::Ptr> bcos::gateway::Service::forwardMessageByNodeID(
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
        co_return co_await directSendMessageByNodeID(
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
    co_return co_await directSendMessageByNodeID(
        std::move(nextHop), header, std::move(payloads), options);
}

bool bcos::gateway::Service::active()
{
    return m_run;
}
bcos::gateway::P2pID bcos::gateway::Service::id() const
{
    return m_nodeID;
}
void bcos::gateway::Service::registerUnreachableHandler(std::function<void(std::string)> _handler)
{
    if (!m_enableRIPProtocol)
    {
        return;
    }
    WriteGuard writeGuard(x_unreachableHandlers);
    m_unreachableHandlers.emplace_back(std::move(_handler));
}
std::map<NodeIPEndpoint, P2pID> bcos::gateway::Service::staticNodes()
{
    // FIB-186 (vector D): read m_staticNodes under x_nodes -- the same member heartBeat now
    // snapshots under the lock. Returning it unlocked races with onConnect's updateStaticNodes
    // writer (the exact data race this fix closes inline in heartBeat).
    std::shared_lock lock(x_nodes);
    return m_staticNodes;
}
void bcos::gateway::Service::setStaticNodes(const std::set<NodeIPEndpoint>& staticNodes)
{
    std::unique_lock nodeLock(x_nodes);
    m_staticNodes.clear();
    for (const auto& endpoint : staticNodes)
    {
        m_staticNodes.insert(std::make_pair(endpoint, ""));
    }
}
bcos::gateway::P2PInfo bcos::gateway::Service::localP2pInfo()
{
    auto p2pInfo = m_host->p2pInfo();
    p2pInfo.p2pID = m_nodeID;
    return p2pInfo;
}
bool bcos::gateway::Service::isReachable(P2pID const& _nodeID) const
{
    if (m_enableRIPProtocol)
    {
        auto reachableNodes = m_routerTable->getAllReachableNode();
        return reachableNodes.contains(_nodeID);
    }
    return isConnected(_nodeID);
}
std::shared_ptr<Host> bcos::gateway::Service::host()
{
    return m_host;
}
void bcos::gateway::Service::setHost(std::shared_ptr<Host> host)
{
    m_host = std::move(host);
}
std::shared_ptr<MessageFactory> bcos::gateway::Service::messageFactory()
{
    return m_messageFactory;
}
void bcos::gateway::Service::setMessageFactory(std::shared_ptr<MessageFactory> _messageFactory)
{
    m_messageFactory = std::move(_messageFactory);
}
std::shared_ptr<bcos::crypto::KeyFactory> bcos::gateway::Service::keyFactory()
{
    return m_keyFactory;
}
void bcos::gateway::Service::setKeyFactory(std::shared_ptr<bcos::crypto::KeyFactory> _keyFactory)
{
    m_keyFactory = std::move(_keyFactory);
}
void bcos::gateway::Service::registerDisconnectHandler(
    std::function<void(NetworkException, P2PSession::Ptr)> _handler)
{
    m_disconnectionHandlers.push_back(std::move(_handler));
}
std::shared_ptr<P2PSession> bcos::gateway::Service::getP2PSessionByNodeIdWithoutLock(
    P2pID const& _nodeID) const
{
    if (auto it = m_sessions.find(_nodeID); it != m_sessions.end())
    {
        return it->second;
    }
    return nullptr;
}


bool bcos::gateway::Service::registerHandlerByMsgType(
    uint16_t _type, MessageHandler const& _msgHandler)
{
    if (m_msgHandlers.at(_type))
    {
        return false;
    }

    m_msgHandlers.at(_type) = _msgHandler;
    return true;
}
bcos::gateway::Service::MessageHandler bcos::gateway::Service::getMessageHandlerByMsgType(
    uint16_t _type)
{
    return m_msgHandlers.at(_type);
}
void bcos::gateway::Service::eraseHandlerByMsgType(uint16_t _type)
{
    m_msgHandlers.at(_type) = nullptr;
}
void bcos::gateway::Service::setBeforeMessageHandler(std::function<std::optional<bcos::Error>(
    Session&, const Message&, uint32_t)> _handler)
{
    m_beforeMessageHandler = std::move(_handler);
}
void bcos::gateway::Service::setOnMessageHandler(
    std::function<std::optional<bcos::Error>(Session::Ptr, Message::Ptr)> _handler)
{
    m_onMessageHandler = std::move(_handler);
}
std::string bcos::gateway::Service::getShortP2pID(std::string const& rawP2pID) const
{
    if (!m_enableRIPProtocol)
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
std::string bcos::gateway::Service::getRawP2pID(std::string const& shortP2pID) const
{
    if (!m_enableRIPProtocol)
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
    bcos::ReadGuard lock(x_p2pIDInfo);
    if (auto it = m_p2pIDInfo.find(shortP2pID); it != m_p2pIDInfo.end())
    {
        return it->second;
    }
    SERVICE2_LOG(WARNING) << LOG_DESC("getRawP2pID failed, return shortP2pID directly")
                          << LOG_KV("id", printShortP2pID(shortP2pID));
    return shortP2pID;
}
void bcos::gateway::Service::resetP2pID(
    P2PMessage& message, bcos::protocol::ProtocolVersion const& version)
{
    if (!m_enableRIPProtocol)
    {
        return;
    }
    // Note: since the message of the old node maybe forwarded through new node, we should try to
    // reset the p2pNodeID in both cases >=v3 and <v3
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
void bcos::gateway::Service::registerOnNewSession(std::function<void(P2PSession::Ptr)> _handler)
{
    m_newSessionHandlers.emplace_back(std::move(_handler));
}
void bcos::gateway::Service::registerOnDeleteSession(std::function<void(P2PSession::Ptr)> _handler)
{
    m_deleteSessionHandlers.emplace_back(std::move(_handler));
}
void bcos::gateway::Service::callNewSessionHandlers(const P2PSession::Ptr& _session)
{
    try
    {
        for (auto const& handler : m_newSessionHandlers)
        {
            handler(_session);
        }
    }
    catch (std::exception const& e)
    {
        SERVICE_LOG(WARNING) << LOG_DESC("callNewSessionHandlers exception")
                             << LOG_KV("msg", boost::diagnostic_information(e));
    }
}
void bcos::gateway::Service::callDeleteSessionHandlers(const P2PSession::Ptr& _session)
{
    try
    {
        for (auto const& handler : m_deleteSessionHandlers)
        {
            handler(_session);
        }
    }
    catch (std::exception const& e)
    {
        SERVICE_LOG(WARNING) << LOG_DESC("callDeleteSessionHandlers exception")
                             << LOG_KV("msg", boost::diagnostic_information(e));
    }
}

// ---- RIP-router mode (formerly ServiceV2) ----

// receive routerTable from peers
void Service::onReceivePeersRouterTable(
    NetworkException _error, std::shared_ptr<P2PSession> _session, P2PMessage::Ptr _message)
{
    if (_error.errorCode() != 0)
    {
        SERVICE2_LOG(WARNING) << LOG_BADGE("onReceivePeersRouterTable")
                              << LOG_KV("code", _error.errorCode()) << LOG_KV("msg", _error.what());
        return;
    }
    auto routerTable = std::make_shared<RouterTable>(_message->payload());

    SERVICE2_LOG(INFO) << LOG_BADGE("onReceivePeersRouterTable")
                       << LOG_KV("peer", _session->printP2pID())
                       << LOG_KV("entrySize", routerTable->routerEntries().size());
    joinRouterTable(_session, routerTable);
}

void Service::joinRouterTable(std::shared_ptr<P2PSession> _session, RouterTable::Ptr _routerTable)
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

    auto entry = std::make_shared<RouterTableEntry>();
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
void Service::onReceiveRouterTableRequest(
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
    auto self = shared_from_this();
    // fire-and-forget through the coroutine fast path: the message is built in the frame and the
    // router table payload is moved into it (the caller's buffer does not outlive the deferred
    // send); an unreachable peer is an expected, recoverable state.
    task::wait([](std::shared_ptr<Service> _self, uint16_t _type, P2pID _nodeID,
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

void Service::broadcastRouterSeq()
{
    m_routerTimer->restart();
    auto seq = m_statusSeq.load();
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
        auto message = std::make_shared<P2PMessageV2>();
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

void Service::onReceiveRouterSeq(
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
    auto self = shared_from_this();
    // fire-and-forget through the coroutine fast path: the message is built in the frame and the
    // (empty) payload rides as a view; an unreachable peer is an expected, recoverable state.
    task::wait([](std::shared_ptr<Service> _self, uint16_t _type, P2pID _nodeID)
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

void Service::onNewSession(P2PSession::Ptr _session)
{
    // update the p2p information when establish new session
    updateP2pInfo(_session->p2pInfo());
    std::set<std::string> unreachableNodes;
    auto entry = std::make_shared<RouterTableEntry>();
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

void Service::onEraseSession(P2PSession::Ptr _session)
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

bool Service::tryToUpdateSeq(std::string const& _p2pNodeID, uint32_t _seq)
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

bool Service::eraseSeq(std::string const& _p2pNodeID)
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

void Service::onP2PNodesUnreachable(std::set<std::string> const& _p2pNodeIDs)
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

void Service::updateP2pInfo(P2PInfo const& p2pInfo)
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

void Service::tryToUpdateRawP2pInfo(P2PInfo const& p2pInfo)
{
    bcos::UpgradableGuard lock(x_rawP2pIDInfo);
    auto it = m_rawP2pIDInfo.find(p2pInfo.rawP2pID);
    if (it != m_rawP2pIDInfo.end())
    {
        return;
    }
    m_rawP2pIDInfo.insert(std::make_pair(p2pInfo.rawP2pID, p2pInfo.p2pID));
}

void Service::tryToUpdateP2pInfo(P2PInfo const& p2pInfo)
{
    bcos::UpgradableGuard lock(x_p2pIDInfo);
    auto it = m_p2pIDInfo.find(p2pInfo.p2pID);
    if (it != m_p2pIDInfo.end())
    {
        return;
    }
    m_p2pIDInfo.insert(std::make_pair(p2pInfo.p2pID, p2pInfo.rawP2pID));
}
