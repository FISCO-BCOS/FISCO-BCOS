/** @file Service.cpp
 *  @author chaychen
 *  @date 20180910
 */

#include "bcos-gateway/libp2p/Service.h"
#include "bcos-framework/Common.h"
#include "bcos-framework/protocol/GlobalConfig.h"
#include "bcos-gateway/libnetwork/Common.h"      // for SocketFace
#include "bcos-gateway/libnetwork/SocketFace.h"  // for SocketFace
#include "bcos-gateway/libp2p/Common.h"
#include "bcos-gateway/libp2p/P2PInterface.h"  // for SessionCallbackFunc...
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
#include <shared_mutex>
#include <utility>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::protocol;

static const uint32_t CHECK_INTERVAL = 10000;

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
                                         std::shared_ptr<SessionFace> session) {
            auto service = self.lock();
            if (service)
            {
                service->onConnect(std::move(e), p2pInfo, std::move(session));
            }
        });
        m_host->start();

        heartBeat();
    }
}

void Service::stop()
{
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
void Service::updateStaticNodes(std::shared_ptr<SocketFace> const& _s, P2pID const& nodeID)
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
    NetworkException e, P2PInfo const& p2pInfo, std::shared_ptr<SessionFace> session)
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
        [this](SessionFace& session, const Message& message, uint32_t wireLength) {
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
            // Same shape as ServiceV2::sendRespMessageBySession (ServiceV2 delegates V0 peers
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
    SessionFace& _session, const Message& _message, uint32_t _wireLength)
{
    if (m_beforeMessageHandler)
    {
        return m_beforeMessageHandler(_session, _message, _wireLength);
    }

    return std::nullopt;
}

void Service::onMessage(NetworkException e, SessionFace::Ptr session, Message::Ptr message,
    std::weak_ptr<P2PSession> p2pSessionWeakPtr)
{
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
    // through the ServiceV2 router table. This preserves the "router table sync only between
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

std::shared_ptr<P2PMessage> Service::newP2PMessage(uint16_t _type, bytesConstRef _payload)
{
    auto message = std::static_pointer_cast<P2PMessage>(messageFactory()->buildMessage());

    message->setPacketType(_type);
    message->setSeq(messageFactory()->newSeq());
    message->setPayload({_payload.begin(), _payload.end()});
    return message;
}

void Service::asyncSendMessageByP2PNodeID(uint16_t _type, P2pID _dstNodeID, bytesConstRef _payload,
    Options _options, P2PResponseCallback _callback)
{
    if (!isReachable(_dstNodeID))
    {
        if (_callback)
        {
            auto errorMsg =
                "send message to " + _dstNodeID + " failed for no connection established";
            _callback(BCOS_ERROR_PTR(-1, errorMsg), 0, {});
        }
        return;
    }
    auto self = shared_from_this();
    // value message in frame; payload owned by the frame and sent as a view (zero-copy). All state
    // is passed as coroutine parameters so it is copied into the frame and stays alive.
    task::wait([](std::shared_ptr<Service> _self, uint16_t _type, P2pID _dstNodeID,
                   bcos::bytes _payload, Options _options,
                   P2PResponseCallback _callback) mutable -> task::Task<void> {
        P2PMessageV2 message;
        message.setPacketType(_type);
        message.setSeq(_self->messageFactory()->newSeq());
        message.setPayload(std::move(_payload));
        if (!_callback)
        {
            // fire-and-forget: an unreachable peer (session dropped between the isReachable check
            // and the send) is an expected, recoverable state — log and continue. sendMessageByNodeID
            // throws NetworkException for an inactive session instead of invoking a callback, so
            // catching it here keeps task::wait from unwinding and aborting the remaining nodes in
            // asyncSendMessageByP2PNodeIDs.
            try
            {
                co_await _self->sendMessageByNodeID(_dstNodeID, message,
                    ::ranges::views::single(message.payload()), Options{_options.timeout, false});
            }
            catch (NetworkException const& e)
            {
                SERVICE_LOG(INFO) << LOG_DESC("asyncSendMessageByP2PNodeID send failed")
                                  << LOG_KV("nodeid", printShortP2pID(_dstNodeID))
                                  << LOG_KV("code", e.errorCode())
                                  << LOG_KV("message", e.what());
            }
            co_return;
        }
        try
        {
            auto resp = co_await _self->sendMessageByNodeID(_dstNodeID, message,
                ::ranges::views::single(message.payload()), Options{_options.timeout, true});
            auto respMessage = std::dynamic_pointer_cast<P2PMessage>(resp);
            auto packetType = respMessage ? respMessage->packetType() : (uint16_t)0;
            _callback(nullptr, packetType,
                respMessage ? respMessage->payload() : bytesConstRef{});
        }
        catch (NetworkException& e)
        {
            _callback(e.toError(), 0, bytesConstRef{});
        }
    }(self, _type, std::move(_dstNodeID),
        bcos::bytes(_payload.begin(), _payload.end()), _options, _callback));
}

void Service::asyncBroadcastMessageToP2PNodes(
    uint16_t _type, uint16_t moduleID, bytesConstRef _payload, Options _options)
{
    auto self = shared_from_this();
    // value message held by shared_ptr: broadcastMessageToAll fans out one coroutine per peer and
    // each task keeps the message alive (zero-copy: the payload rides as a view). All state is
    // passed as coroutine parameters so it is copied into the frame and stays alive.
    task::wait([](std::shared_ptr<Service> _self, uint16_t _type, bcos::bytes _payload,
                   Options _options) mutable -> task::Task<void> {
        auto message = std::make_shared<P2PMessageV2>();
        message->setPacketType(_type);
        message->setSeq(_self->messageFactory()->newSeq());
        message->setPayload(std::move(_payload));
        co_await _self->broadcastMessageToAll(
            message, ::ranges::views::single(message->payload()), _options);
    }(self, _type, bcos::bytes(_payload.begin(), _payload.end()), _options));
}

void Service::asyncSendMessageByP2PNodeIDs(
    uint16_t _type, const std::vector<P2pID>& _nodeIDs, bytesConstRef _payload, Options _options)
{
    for (auto const& nodeID : _nodeIDs)
    {
        asyncSendMessageByP2PNodeID(_type, nodeID, _payload, _options, nullptr);
    }
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
void Service::Service::onReceiveHeartbeat(
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
        for (auto const& session : m_sessions)
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
bool bcos::gateway::Service::active()
{
    return m_run;
}
bcos::gateway::P2pID bcos::gateway::Service::id() const
{
    return m_nodeID;
}
void bcos::gateway::Service::registerUnreachableHandler(std::function<void(std::string)> /*unused*/)
{}
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
bcos::gateway::P2PInterface::MessageHandler bcos::gateway::Service::getMessageHandlerByMsgType(
    uint16_t _type)
{
    return m_msgHandlers.at(_type);
}
void bcos::gateway::Service::eraseHandlerByMsgType(uint16_t _type)
{
    m_msgHandlers.at(_type) = nullptr;
}
void bcos::gateway::Service::setBeforeMessageHandler(std::function<std::optional<bcos::Error>(
    SessionFace&, const Message&, uint32_t)> _handler)
{
    m_beforeMessageHandler = std::move(_handler);
}
void bcos::gateway::Service::setOnMessageHandler(
    std::function<std::optional<bcos::Error>(SessionFace::Ptr, Message::Ptr)> _handler)
{
    m_onMessageHandler = std::move(_handler);
}
std::string bcos::gateway::Service::getShortP2pID(std::string const& rawP2pID) const
{
    return rawP2pID;
}
std::string bcos::gateway::Service::getRawP2pID(std::string const& shortP2pID) const
{
    return shortP2pID;
}
void bcos::gateway::Service::resetP2pID(P2PMessage&, bcos::protocol::ProtocolVersion const&) {}
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