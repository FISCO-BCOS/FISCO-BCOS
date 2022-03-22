/** @file Service.cpp
 *  @author chaychen
 *  @date 20180910
 */

#include <bcos-framework/interfaces/protocol/CommonError.h>
#include <bcos-gateway/libnetwork/ASIOInterface.h>  // for ASIOInterface
#include <bcos-gateway/libnetwork/Common.h>         // for SocketFace
#include <bcos-gateway/libnetwork/SocketFace.h>     // for SocketFace
#include <bcos-gateway/libp2p/Common.h>
#include <bcos-gateway/libp2p/P2PInterface.h>  // for SessionCallbackFunc...
#include <bcos-gateway/libp2p/P2PMessage.h>
#include <bcos-gateway/libp2p/P2PSession.h>  // for P2PSession
#include <bcos-gateway/libp2p/Service.h>
#include <boost/random.hpp>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::protocol;

static const uint32_t CHECK_INTERVEL = 10000;

Service::Service()
{
    m_localProtocol = g_BCOSConfig.protocolInfo(ProtocolModuleID::GatewayService);
    m_codec = g_BCOSConfig.codec();
    // Process handshake packet logic, handshake protocol and determine
    // the version, when handshake finished the version field of P2PMessage
    // should be set
    registerHandlerByMsgType(MessageType::Handshake,
        boost::bind(&Service::onReceiveProtocol, this, boost::placeholders::_1,
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
        RecursiveGuard l(x_sessions);
        for (auto session : m_sessions)
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
            SERVICE_LOG(DEBUG) << LOG_DESC("heartBeat ignore myself p2pid same")
                               << LOG_KV("remote endpoint", it.first)
                               << LOG_KV("nodeid", it.second);
            continue;
        }
        if (!it.second.empty() && isConnected(it.second))
        {
            SERVICE_LOG(DEBUG) << LOG_DESC("heartBeat ignore connected")
                               << LOG_KV("endpoint", it.first) << LOG_KV("nodeid", it.second);
            continue;
        }
        SERVICE_LOG(DEBUG) << LOG_DESC("heartBeat try to reconnect")
                           << LOG_KV("endpoint", it.first);
        m_host->asyncConnect(
            it.first, std::bind(&Service::onConnect, shared_from_this(), std::placeholders::_1,
                          std::placeholders::_2, std::placeholders::_3));
    }
    {
        RecursiveGuard l(x_sessions);
        SERVICE_LOG(INFO) << LOG_DESC("heartBeat") << LOG_KV("connected count", m_sessions.size());
    }

    auto self = std::weak_ptr<Service>(shared_from_this());
    m_timer = m_host->asioInterface()->newTimer(CHECK_INTERVEL);
    m_timer->async_wait([self](const boost::system::error_code& error) {
        if (error)
        {
            SERVICE_LOG(WARNING) << "timer canceled" << LOG_KV("errorCode", error);
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
        SERVICE_LOG(WARNING) << LOG_DESC("onConnect") << LOG_KV("errorCode", e.errorCode())
                             << LOG_KV("p2pid", p2pID) << LOG_KV("nodeName", p2pInfo.nodeName)
                             << LOG_KV("endpoint", peer) << LOG_KV("errorMessage", e.what());

        return;
    }

    SERVICE_LOG(INFO) << LOG_DESC("onConnect") << LOG_KV("p2pid", p2pID)
                      << LOG_KV("endpoint", peer);

    RecursiveGuard l(x_sessions);
    auto it = m_sessions.find(p2pID);
    if (it != m_sessions.end() && it->second->actived())
    {
        SERVICE_LOG(INFO) << "Disconnect duplicate peer" << LOG_KV("p2pid", p2pID);
        updateStaticNodes(session->socket(), p2pID);
        session->disconnect(DuplicatePeer);
        return;
    }

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
    p2pSession->setService(std::weak_ptr<Service>(shared_from_this()));

    auto p2pSessionWeakPtr = std::weak_ptr<P2PSession>(p2pSession);
    p2pSession->session()->setMessageHandler(std::bind(&Service::onMessage, shared_from_this(),
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, p2pSessionWeakPtr));
    p2pSession->start();
    asyncSendProtocol(p2pSession);
    updateStaticNodes(session->socket(), p2pID);
    if (it != m_sessions.end())
    {
        it->second = p2pSession;
    }
    else
    {
        m_sessions.insert(std::make_pair(p2pID, p2pSession));
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

    RecursiveGuard l(x_sessions);
    auto it = m_sessions.find(p2pSession->p2pID());
    if (it != m_sessions.end() && it->second == p2pSession)
    {
        SERVICE_LOG(TRACE) << "Service onDisconnect and remove from m_sessions"
                           << LOG_KV("p2pid", p2pSession->p2pID())
                           << LOG_KV("endpoint", p2pSession->session()->nodeIPEndpoint());

        m_sessions.erase(it);
        if (e.errorCode() == P2PExceptionType::DuplicateSession)
            return;
        SERVICE_LOG(WARNING) << LOG_DESC("onDisconnect") << LOG_KV("errorCode", e.errorCode())
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
}

void Service::sendMessageBySession(
    int _packetType, bytesConstRef _payload, P2PSession::Ptr _p2pSession)
{
    auto p2pMessage = std::static_pointer_cast<P2PMessage>(messageFactory()->buildMessage());
    auto seq = messageFactory()->newSeq();
    p2pMessage->setSeq(seq);
    p2pMessage->setPacketType(_packetType);
    p2pMessage->setPayload(std::make_shared<bytes>(_payload.begin(), _payload.end()));

    _p2pSession->session()->asyncSendMessage(p2pMessage);

    SERVICE_LOG(TRACE) << "sendMessageBySession" << LOG_KV("seq", p2pMessage->seq())
                       << LOG_KV("packetType", _packetType) << LOG_KV("p2pid", _p2pSession->p2pID())
                       << LOG_KV("payload.size()", _payload.size());
}

void Service::sendRespMessageBySession(
    bytesConstRef _payload, P2PMessage::Ptr _p2pMessage, P2PSession::Ptr _p2pSession)
{
    auto respMessage = std::static_pointer_cast<P2PMessage>(messageFactory()->buildMessage());

    respMessage->setSeq(_p2pMessage->seq());
    respMessage->setRespPacket();
    respMessage->setPayload(std::make_shared<bytes>(_payload.begin(), _payload.end()));

    _p2pSession->session()->asyncSendMessage(respMessage);

    SERVICE_LOG(TRACE) << "sendRespMessageBySession" << LOG_KV("seq", _p2pMessage->seq())
                       << LOG_KV("p2pid", _p2pSession->p2pID())
                       << LOG_KV("payload size", _payload.size());
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
            SERVICE_LOG(WARNING) << LOG_DESC("disconnect error P2PSession")
                                 << LOG_KV("p2pid", p2pID) << LOG_KV("endpoint", nodeIPEndpoint)
                                 << LOG_KV("errorCode", e.errorCode())
                                 << LOG_KV("errorMessage", e.what());

            if (p2pSession)
            {
                p2pSession->stop(UserReason);
                onDisconnect(e, p2pSession);
            }
            return;
        }

        /// SERVICE_LOG(TRACE) << "Service onMessage: " << message->seq();
        auto p2pMessage = std::dynamic_pointer_cast<P2PMessage>(message);
        SERVICE_LOG(TRACE) << LOG_DESC("onMessage receive message") << LOG_KV("p2pid", p2pID)
                           << LOG_KV("endpoint", nodeIPEndpoint) << LOG_KV("seq", p2pMessage->seq())
                           << LOG_KV("version", p2pMessage->version())
                           << LOG_KV("packetType", p2pMessage->packetType());

        auto packetType = p2pMessage->packetType();
        auto handler = getMessageHandlerByMsgType(packetType);
        if (handler)
        {
            // TODO: use threadpool here
            handler(e, p2pSession, p2pMessage);
            return;
        }
        switch (packetType)
        {
        case MessageType::Heartbeat:
            break;
        default:
        {
            SERVICE_LOG(ERROR) << LOG_DESC("Unrecognized message type")
                               << LOG_KV("packetType", packetType)
                               << LOG_KV("packetSeq", message->seq());
        }
        break;
        };
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

    return P2PMessage::Ptr();
}

bool Service::connected(std::string const& _nodeID)
{
    RecursiveGuard l(x_sessions);
    auto it = m_sessions.find(_nodeID);
    return (it != m_sessions.end() && it->second->actived());
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

        RecursiveGuard l(x_sessions);
        auto it = m_sessions.find(nodeID);

        if (it != m_sessions.end() && it->second->actived())
        {
            if (message->seq() == 0)
            {
                message->setSeq(m_messageFactory->newSeq());
            }
            auto session = it->second;
            if (callback)
            {
                session->session()->asyncSendMessage(message, options,
                    [session, callback](NetworkException e, Message::Ptr message) {
                        P2PMessage::Ptr p2pMessage = std::dynamic_pointer_cast<P2PMessage>(message);
                        if (callback)
                        {
                            callback(e, session, p2pMessage);
                        }
                    });
            }
            else
            {
                session->session()->asyncSendMessage(message, options, nullptr);
            }
        }
        else
        {
            if (callback)
            {
                NetworkException e(-1, "send message failed for no network established");
                callback(e, nullptr, nullptr);
            }
            SERVICE_LOG(WARNING) << "Node inactived" << LOG_KV("nodeid", nodeID);
        }
    }
    catch (std::exception& e)
    {
        SERVICE_LOG(ERROR) << "asyncSendMessageByNodeID" << LOG_KV("nodeid", nodeID)
                           << LOG_KV("what", boost::diagnostic_information(e));

        if (callback)
        {
            m_host->threadPool()->enqueue([callback, e] {
                callback(NetworkException(Disconnect, "Disconnect"), P2PSession::Ptr(),
                    P2PMessage::Ptr());
            });
        }
    }
}

void Service::asyncBroadcastMessage(P2PMessage::Ptr message, Options options)
{
    try
    {
        std::unordered_map<P2pID, P2PSession::Ptr> sessions;
        {
            RecursiveGuard l(x_sessions);
            sessions = m_sessions;
        }

        for (auto s : sessions)
        {
            asyncSendMessageByNodeID(s.first, message, CallbackFuncWithSession(), options);
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
        RecursiveGuard l(x_sessions);
        auto s = m_sessions;
        for (auto const& i : s)
        {
            infos.push_back(i.second->p2pInfo());
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
    RecursiveGuard l(x_sessions);
    auto it = m_sessions.find(nodeID);

    if (it != m_sessions.end() && it->second->actived())
    {
        return true;
    }
    return false;
}

std::shared_ptr<P2PMessage> Service::newP2PMessage(int16_t _type, bytesConstRef _payload)
{
    auto message = std::static_pointer_cast<P2PMessage>(messageFactory()->buildMessage());

    message->setPacketType(_type);
    message->setSeq(messageFactory()->newSeq());
    message->setPayload(std::make_shared<bytes>(_payload.begin(), _payload.end()));
    return message;
}

void Service::asyncSendMessageByP2PNodeID(int16_t _type, P2pID _dstNodeID, bytesConstRef _payload,
    Options _options, P2PResponseCallback _callback)
{
    if (!connected(_dstNodeID))
    {
        if (_callback)
        {
            auto errorMsg =
                "send message to " + _dstNodeID + " failed for no connection established";
            _callback(std::make_shared<bcos::Error>(-1, errorMsg), 0, nullptr);
        }
        return;
    }
    auto p2pMessage = newP2PMessage(_type, _payload);
    asyncSendMessageByNodeID(
        _dstNodeID, p2pMessage,
        [_dstNodeID, _callback](NetworkException _e, std::shared_ptr<P2PSession>,
            std::shared_ptr<P2PMessage> _p2pMessage) {
            auto packetType = _p2pMessage ? _p2pMessage->packetType() : 0;
            if (_e.errorCode() != 0)
            {
                SERVICE_LOG(WARNING) << LOG_DESC("asyncSendMessageByP2PNodeID error")
                                     << LOG_KV("code", _e.errorCode()) << LOG_KV("msg", _e.what())
                                     << LOG_KV("type", packetType) << LOG_KV("dst", _dstNodeID);
                if (_callback)
                {
                    _callback(
                        _e.toError(), packetType, _p2pMessage ? _p2pMessage->payload() : nullptr);
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
    int16_t _type, bytesConstRef _payload, Options _options)
{
    auto p2pMessage = newP2PMessage(_type, _payload);
    asyncBroadcastMessage(p2pMessage, _options);
}

void Service::asyncSendMessageByP2PNodeIDs(
    int16_t _type, const std::vector<P2pID>& _nodeIDs, bytesConstRef _payload, Options _options)
{
    for (auto const& nodeID : _nodeIDs)
    {
        asyncSendMessageByP2PNodeID(_type, nodeID, _payload, _options, nullptr);
    }
}

// send the protocolInfo
void Service::asyncSendProtocol(P2PSession::Ptr _session)
{
    auto payload = std::make_shared<bytes>();
    m_codec->encode(m_localProtocol, *payload);
    auto message = std::static_pointer_cast<P2PMessage>(messageFactory()->buildMessage());
    message->setPacketType(MessageType::Handshake);
    auto seq = messageFactory()->newSeq();
    message->setSeq(seq);
    message->setPayload(payload);

    SERVICE_LOG(INFO) << LOG_DESC("asyncSendProtocol") << LOG_KV("payload", payload->size())
                      << LOG_KV("seq", seq);
    _session->session()->asyncSendMessage(message, Options(), nullptr);
}

// receive the protocolInfo
void Service::onReceiveProtocol(
    NetworkException _e, std::shared_ptr<P2PSession> _session, P2PMessage::Ptr _message)
{
    if (_e.errorCode())
    {
        SERVICE_LOG(WARNING) << LOG_DESC("onReceiveProtocol error")
                             << LOG_KV("errorCode", _e.errorCode()) << LOG_KV("errorMsg", _e.what())
                             << LOG_KV("peer", _session ? _session->p2pID() : "unknown");
        return;
    }
    try
    {
        auto payload = _message->payload();
        auto protocolInfo = m_codec->decode(bytesConstRef(payload->data(), payload->size()));
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
        protocolInfo->setVersion((ProtocolVersion)version);
        _session->setProtocolInfo(protocolInfo);
        SERVICE_LOG(WARNING) << LOG_DESC("onReceiveProtocol: protocolNegotiate success")
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
