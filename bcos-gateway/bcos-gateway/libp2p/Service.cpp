/** @file Service.cpp
 *  @author chaychen
 *  @date 20180910
 */

#include "bcos-utilities/BoostLog.h"
#include <bcos-framework/protocol/CommonError.h>
#include <bcos-gateway/libnetwork/ASIOInterface.h>  // for ASIOInterface
#include <bcos-gateway/libnetwork/Common.h>         // for SocketFace
#include <bcos-gateway/libnetwork/SocketFace.h>     // for SocketFace
#include <bcos-gateway/libp2p/Common.h>
#include <bcos-gateway/libp2p/P2PInterface.h>  // for SessionCallbackFunc...
#include <bcos-gateway/libp2p/P2PMessage.h>
#include <bcos-gateway/libp2p/P2PSession.h>  // for P2PSession
#include <bcos-gateway/libp2p/Service.h>
#include <boost/random.hpp>
#include <boost/throw_exception.hpp>
#include <utility>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::protocol;

static const uint32_t CHECK_INTERVAL = 10000;

Service::Service(std::string const& _nodeID) : m_nodeID(_nodeID)
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
        boost::bind(&Service::onReceiveProtocol, this, boost::placeholders::_1,
            boost::placeholders::_2, boost::placeholders::_3));

    registerHandlerByMsgType(GatewayMessageType::Heartbeat,
        boost::bind(&Service::onReceiveHeartbeat, this, boost::placeholders::_1,
            boost::placeholders::_2, boost::placeholders::_3));
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
                service->onConnect(e, p2pInfo, session);
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

    std::map<NodeIPEndpoint, P2pID> staticNodes;
    {
        RecursiveGuard l(x_nodes);
        staticNodes = m_staticNodes;
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
                               << LOG_KV("endpoint", it.first) << LOG_KV("nodeid", it.second);
            continue;
        }
        SERVICE_LOG(DEBUG) << LOG_DESC("heartBeat try to reconnect")
                           << LOG_KV("endpoint", it.first);
        m_host->asyncConnect(it.first,
            [service = shared_from_this()](auto error, const P2PInfo& p2pInfo, auto session) {
                service->onConnect(std::move(error), p2pInfo, std::move(session));
            });
    }

    SERVICE_LOG(INFO) << METRIC << LOG_DESC("heartBeat")
                      << LOG_KV("connected count", m_sessions.size());
    for (auto& [p2pID, session] : m_sessions)
    {
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
    RecursiveGuard l(x_nodes);
    auto it = m_staticNodes.find(endpoint);
    // modify m_staticNodes(including accept cases, namely the client endpoint)
    if (it != m_staticNodes.end())
    {
        SERVICE_LOG(INFO) << LOG_DESC("updateStaticNodes") << LOG_KV("nodeid", nodeID)
                          << LOG_KV("endpoint", endpoint);
        it->second = nodeID;
    }
    else
    {
        SERVICE_LOG(DEBUG) << LOG_DESC("updateStaticNodes can't find endpoint")
                           << LOG_KV("nodeid", nodeID) << LOG_KV("endpoint", endpoint);
    }
}

void Service::onConnect(
    NetworkException e, P2PInfo const& p2pInfo, std::shared_ptr<SessionFace> session)
{
    P2pID p2pID = p2pInfo.p2pID;
    std::string peer = "unknown";
    if (session)
    {
        peer = session->nodeIPEndpoint().address();
    }
    if (e.errorCode())
    {
        SERVICE_LOG(WARNING) << LOG_DESC("onConnect") << LOG_KV("code", e.errorCode())
                             << LOG_KV("p2pid", p2pID) << LOG_KV("nodeName", p2pInfo.nodeName)
                             << LOG_KV("endpoint", peer) << LOG_KV("message", e.what());

        return;
    }

    SERVICE_LOG(INFO) << LOG_DESC("onConnect") << LOG_KV("p2pid", p2pID)
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
    p2pSession->session()->setBeforeMessageHandler([this](SessionFace& session, Message& message) {
        return onBeforeMessage(session, message);
    });

    decltype(m_sessions)::accessor accessor;
    if (m_sessions.find(accessor, p2pID) && accessor->second->active())
    {
        SERVICE_LOG(INFO) << "Disconnect duplicate peer" << LOG_KV("p2pid", p2pID);
        updateStaticNodes(session->socket(), p2pID);
        session->disconnect(DuplicatePeer);
        return;
    }
    p2pSession->start();
    asyncSendProtocol(p2pSession);
    updateStaticNodes(session->socket(), p2pID);
    if (!accessor.empty())
    {
        accessor->second = p2pSession;
    }
    else
    {
        m_sessions.insert(std::make_pair(p2pID, p2pSession));
        accessor.release();
        callNewSessionHandlers(p2pSession);
    }
    SERVICE_LOG(INFO) << LOG_DESC("Connection established") << LOG_KV("p2pid", p2pID)
                      << LOG_KV("endpoint", session->nodeIPEndpoint());
}

void Service::onDisconnect(NetworkException e, P2PSession::Ptr p2pSession)
{
    // handle all registered handlers
    for (const auto& handler : m_disconnectionHandlers)
    {
        handler(e, p2pSession);
    }

    if (decltype(m_sessions)::const_accessor accessor;
        m_sessions.find(accessor, p2pSession->p2pID()) && accessor->second == p2pSession)
    {
        SERVICE_LOG(TRACE) << "Service onDisconnect and remove from m_sessions"
                           << LOG_KV("p2pid", p2pSession->p2pID())
                           << LOG_KV("endpoint", p2pSession->session()->nodeIPEndpoint());

        m_sessions.erase(accessor);
        callDeleteSessionHandlers(p2pSession);

        if (e.errorCode() == P2PExceptionType::DuplicateSession)
        {
            return;
        }
        SERVICE_LOG(INFO) << LOG_DESC("onDisconnect") << LOG_KV("code", e.errorCode())
                          << LOG_KV("what", boost::diagnostic_information(e));
        RecursiveGuard l(x_nodes);
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

void Service::sendMessageToSession(P2PSession::Ptr _p2pSession, P2PMessage::Ptr _msg,
    Options _options, CallbackFuncWithSession _callback)
{
    auto protocolVersion = _p2pSession->protocolInfo()->version();
    _msg->setVersion(protocolVersion);
    if (!_callback)
    {
        _p2pSession->session()->asyncSendMessage(_msg, _options, nullptr);
        return;
    }
    auto weakSession = std::weak_ptr<P2PSession>(_p2pSession);
    _p2pSession->session()->asyncSendMessage(
        _msg, _options, [weakSession, _callback](NetworkException e, Message::Ptr message) {
            auto session = weakSession.lock();
            if (!session)
            {
                return;
            }
            P2PMessage::Ptr p2pMessage = std::dynamic_pointer_cast<P2PMessage>(message);
            if (_callback)
            {
                _callback(e, session, p2pMessage);
            }
        });
}

void Service::sendRespMessageBySession(
    bytesConstRef _payload, P2PMessage::Ptr _p2pMessage, P2PSession::Ptr _p2pSession)
{
    auto respMessage = std::static_pointer_cast<P2PMessage>(messageFactory()->buildMessage());

    respMessage->setSeq(_p2pMessage->seq());
    respMessage->setRespPacket();
    respMessage->setPayload({_payload.begin(), _payload.end()});

    sendMessageToSession(_p2pSession, respMessage);
    SERVICE_LOG(TRACE) << "sendRespMessageBySession" << LOG_KV("seq", _p2pMessage->seq())
                       << LOG_KV("p2pid", _p2pSession->p2pID())
                       << LOG_KV("payload size", _payload.size());
}

std::optional<bcos::Error> Service::onBeforeMessage(SessionFace& _session, Message& _message)
{
    if (m_beforeMessageHandler)
    {
        return m_beforeMessageHandler(_session, _message);
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
            SERVICE_LOG(INFO) << LOG_DESC("disconnect error P2PSession") << LOG_KV("p2pid", p2pID)
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
                               << LOG_KV("p2pid", P2PMessage::printP2PIDElegantly(p2pID))
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
                               << LOG_KV("dst p2p", p2pMessage->dstP2PNodeID());
            return;
        }
        SERVICE_LOG(ERROR) << LOG_DESC("Unrecognized message type") << LOG_KV("seq", message->seq())
                           << LOG_KV("packetType", packetType) << LOG_KV("ext", ext)
                           << LOG_KV("version", version)
                           << LOG_KV("dst p2p", p2pMessage->dstP2PNodeID());
    }
    catch (std::exception& e)
    {
        SERVICE_LOG(ERROR) << "onMessage error" << LOG_KV("what", boost::diagnostic_information(e));
    }
}

P2PMessage::Ptr Service::sendMessageByNodeID(P2pID nodeID, P2PMessage::Ptr message)
{
    try
    {
        struct SessionCallback : public std::enable_shared_from_this<SessionCallback>
        {
        public:
            using Ptr = std::shared_ptr<SessionCallback>;

            SessionCallback() { mutex.lock(); }

            void onResponse(
                NetworkException _error, std::shared_ptr<P2PSession>, P2PMessage::Ptr _message)
            {
                error = _error;
                response = _message;
                mutex.unlock();
            }

            NetworkException error;
            P2PMessage::Ptr response;
            std::mutex mutex;
        };

        SessionCallback::Ptr callback = std::make_shared<SessionCallback>();
        CallbackFuncWithSession fp = std::bind(&SessionCallback::onResponse, callback,
            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

        asyncSendMessageByNodeID(nodeID, message, fp, Options());
        // lock to wait for async send
        callback->mutex.lock();
        callback->mutex.unlock();
        SERVICE_LOG(DEBUG) << LOG_DESC("sendMessageByNodeID mutex unlock");

        NetworkException error = callback->error;
        if (error.errorCode() != 0)
        {
            SERVICE_LOG(ERROR) << LOG_DESC("asyncSendMessageByNodeID error")
                               << LOG_KV("nodeid", nodeID) << LOG_KV("errorCode", error.errorCode())
                               << LOG_KV("what", error.what());
            BOOST_THROW_EXCEPTION(error);
        }

        return callback->response;
    }
    catch (std::exception& e)
    {
        SERVICE_LOG(ERROR) << LOG_DESC("asyncSendMessageByNodeID error") << LOG_KV("nodeid", nodeID)
                           << LOG_KV("what", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(e);
    }

    return {};
}

void Service::asyncSendMessageByEndPoint(NodeIPEndpoint const& _endpoint, P2PMessage::Ptr message,
    CallbackFuncWithSession callback, Options options)
{
    for (auto const& it : m_sessions)
    {
        if (it.second->session()->nodeIPEndpoint() == _endpoint)
        {
            sendMessageToSession(it.second, message, options, callback);
            break;
        }
    }
}

void Service::asyncSendMessageByNodeID(
    P2pID nodeID, P2PMessage::Ptr message, CallbackFuncWithSession callback, Options options)
{
    try
    {
        if (nodeID == id())
        {
            // ignore myself
            return;
        }

        P2PSession::Ptr session;
        if (decltype(m_sessions)::const_accessor accessor;
            m_sessions.find(accessor, nodeID) && accessor->second->active())
        {
            session = accessor->second;
        }

        if (session)
        {
            if (message->seq() == 0)
            {
                message->setSeq(m_messageFactory->newSeq());
            }
            // for compatibility_version consideration
            sendMessageToSession(std::move(session), message, options, callback);
        }
        else
        {
            if (callback)
            {
                NetworkException e(-1, "send message failed for no network established");
                callback(e, nullptr, nullptr);
            }
            SERVICE_LOG(INFO) << "Node inactive" << LOG_KV("nodeid", nodeID);
        }
    }
    catch (std::exception& e)
    {
        SERVICE_LOG(ERROR) << "asyncSendMessageByNodeID" << LOG_KV("nodeid", nodeID)
                           << LOG_KV("what", boost::diagnostic_information(e));

        if (callback)
        {
            callback(
                NetworkException(Disconnect, "Disconnect"), P2PSession::Ptr(), P2PMessage::Ptr());
        }
    }
}

void Service::asyncBroadcastMessage(P2PMessage::Ptr message, Options options)
{
    try
    {
        for (auto& session : m_sessions)
        {
            asyncSendMessageByNodeID(session.first, message, {}, options);
        }
    }
    catch (std::exception& e)
    {
        SERVICE_LOG(WARNING) << LOG_DESC("asyncBroadcastMessage")
                             << LOG_KV("what", boost::diagnostic_information(e));
    }
}

P2PInfos Service::sessionInfos()
{
    P2PInfos infos;
    try
    {
        for (auto const& session : m_sessions)
        {
            infos.push_back(session.second->p2pInfo());
        }
    }
    catch (std::exception& e)
    {
        SERVICE_LOG(WARNING) << LOG_DESC("sessionInfos")
                             << LOG_KV("what", boost::diagnostic_information(e));
    }
    return infos;
}

bool Service::isConnected(P2pID const& nodeID) const
{
    if (decltype(m_sessions)::const_accessor accessor;
        m_sessions.find(accessor, nodeID) && accessor->second->active())
    {
        return true;
    }
    return false;
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
    auto p2pMessage = newP2PMessage(_type, _payload);

    if (!_callback)
    {
        asyncSendMessageByNodeID(_dstNodeID, p2pMessage, nullptr);
        return;
    }

    asyncSendMessageByNodeID(
        _dstNodeID, p2pMessage,
        [_dstNodeID, _callback](NetworkException _e, std::shared_ptr<P2PSession>,
            std::shared_ptr<P2PMessage> _p2pMessage) {
            auto packetType = _p2pMessage ? _p2pMessage->packetType() : (uint16_t)0;
            if (_e.errorCode() != 0)
            {
                SERVICE_LOG(INFO) << LOG_DESC("asyncSendMessageByP2PNodeID error")
                                  << LOG_KV("code", _e.errorCode()) << LOG_KV("msg", _e.what())
                                  << LOG_KV("type", packetType) << LOG_KV("dst", _dstNodeID);
                if (_callback)
                {
                    _callback(_e.toError(), packetType,
                        _p2pMessage ? _p2pMessage->payload() : bytesConstRef{});
                }
                return;
            }
            if (_callback)
            {
                _callback(nullptr, packetType, _p2pMessage->payload());
            }
        },
        _options);
}

void Service::asyncBroadcastMessageToP2PNodes(
    uint16_t _type, uint16_t moduleID, bytesConstRef _payload, Options _options)
{
    auto p2pMessage = newP2PMessage(_type, _payload);
    asyncBroadcastMessage(p2pMessage, _options);
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
    auto payload = bytes();
    m_codec->encode(m_localProtocol, payload);
    auto message = std::static_pointer_cast<P2PMessage>(messageFactory()->buildMessage());
    message->setPacketType(GatewayMessageType::Handshake);
    auto seq = messageFactory()->newSeq();
    message->setSeq(seq);
    message->setPayload(payload);

    SERVICE_LOG(INFO) << LOG_DESC("asyncSendProtocol") << LOG_KV("payload", payload.size())
                      << LOG_KV("seq", seq);
    sendMessageToSession(_session, message, Options(), nullptr);
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
                             << LOG_KV("peer", _session ? _session->p2pID() : "unknown");
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
                << LOG_KV("peer", _session->p2pID())
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
                          << LOG_KV("peer", _session->p2pID())
                          << LOG_KV("minVersion", protocolInfo->minVersion())
                          << LOG_KV("maxVersion", protocolInfo->maxVersion())
                          << LOG_KV("supportMinVersion", m_localProtocol->minVersion())
                          << LOG_KV("supportMaxVersion", m_localProtocol->maxVersion())
                          << LOG_KV("negotiatedVersion", version);
    }
    catch (std::exception const& e)
    {
        SERVICE_LOG(WARNING) << LOG_DESC("onReceiveProtocol exception")
                             << LOG_KV("peer", _session ? _session->p2pID() : "unknown")
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
        for (auto& session : m_sessions)
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
        // ignore myself
        co_return {};
    }

    P2PSession::Ptr session;
    if (decltype(m_sessions)::const_accessor accessor; m_sessions.find(accessor, nodeID))
    {
        session = accessor->second;
    }

    if (!session)
    {
        BOOST_THROW_EXCEPTION(
            NetworkException(-1, "send message failed for no network established"));
    }

    co_return co_await session->session()->sendMessage(header, std::move(payloads), {});
}
