/** @file Service.cpp
 *  @author chaychen
 *  @date 20180910
 */

#include <bcos-boostssl/context/NodeInfoTools.h>
#include <bcos-boostssl/interfaces/MessageFace.h>
#include <bcos-framework/interfaces/protocol/CommonError.h>
#include <bcos-gateway/libp2p/Common.h>
#include <bcos-gateway/libp2p/P2PInterface.h>  // for SessionCallbackFunc...
#include <bcos-gateway/libp2p/P2PMessage.h>
#include <bcos-gateway/libp2p/P2PSession.h>  // for P2PSession
#include <bcos-gateway/libp2p/Service.h>
#include <boost/random.hpp>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::protocol;
using namespace bcos::boostssl;
using namespace bcos::boostssl::ws;
using namespace bcos::boostssl::context;

Service::Service(std::shared_ptr<boostssl::ws::WsService> _wsService) : m_wsService(_wsService)
{
    m_localProtocol = g_BCOSConfig.protocolInfo(ProtocolModuleID::GatewayService);
    m_codec = g_BCOSConfig.codec();

    // Process handshake packet logic, handshake protocol and determine
    // the version, when handshake finished the version field of P2PMessage
    // should be set
    registerHandlerByMsgType(
        GatewayMessageType::Handshake, boost::bind(&Service::onReceiveProtocol, this,
                                           boost::placeholders::_1, boost::placeholders::_2));
}

void Service::start()
{
    if (!m_run)
    {
        m_run = true;

        auto self = std::weak_ptr<Service>(shared_from_this());
        m_wsService->registerConnectHandler([self](std::shared_ptr<WsSession> session) {
            auto service = self.lock();
            if (service)
            {
                service->onConnect(session);
            }
        });
        m_wsService->start();
    }
}

void Service::stop()
{
    if (m_run)
    {
        m_run = false;
        m_wsService->stop();

        /// clear sessions
        m_sessions.clear();
    }
}

std::string Service::obtainCommonNameFromSubject(std::string const& subject)
{
    std::vector<std::string> fields;
    boost::split(fields, subject, boost::is_any_of("/"), boost::token_compress_on);
    for (auto field : fields)
    {
        std::size_t pos = field.find("CN");
        if (pos != std::string::npos)
        {
            std::vector<std::string> cn_fields;
            boost::split(cn_fields, field, boost::is_any_of("="), boost::token_compress_on);
            /// use the whole fields as the common name
            if (cn_fields.size() < 2)
            {
                return field;
            }
            /// return real common name
            return cn_fields[1];
        }
    }
    return subject;
}

P2pInfo Service::localP2pInfo()
{
    try
    {
        if (m_p2pInfo.p2pID.empty())
        {
            /// get certificate
            auto sslContext = m_wsService->ctx()->native_handle();
            X509* cert = SSL_CTX_get0_certificate(sslContext);

            /// get issuer name
            const char* issuer = X509_NAME_oneline(X509_get_issuer_name(cert), NULL, 0);
            std::string issuerName(issuer);

            /// get subject name
            const char* subject = X509_NAME_oneline(X509_get_subject_name(cert), NULL, 0);
            std::string subjectName(subject);

            if (!m_p2pID.empty())
            {
                m_p2pInfo.p2pID = m_p2pID;
            }
            else
            {
                /// get p2pID
                std::string nodeIDOut;
                auto sslContextPubHandler = NodeInfoTools::initSSLContextPubHexHandler();
                if (sslContextPubHandler(cert, nodeIDOut))
                {
                    m_p2pInfo.p2pID = boost::to_upper_copy(nodeIDOut);
                    SERVICE_LOG(INFO) << LOG_DESC("Get node information from cert")
                                      << LOG_KV("p2pID", m_p2pInfo.p2pID);
                }
            }

            /// fill in the node informations
            m_p2pInfo.agencyName = obtainCommonNameFromSubject(issuerName);
            m_p2pInfo.nodeName = obtainCommonNameFromSubject(subjectName);
            m_p2pInfo.hostIp = m_wsService->listenHost();
            m_p2pInfo.hostPort = boost::lexical_cast<std::string>(m_wsService->listenPort());
            /// free resources
            OPENSSL_free((void*)issuer);
            OPENSSL_free((void*)subject);
        }
    }
    catch (std::exception& e)
    {
        SERVICE_LOG(ERROR) << LOG_DESC("Get node information from cert failed.")
                           << boost::diagnostic_information(e);
        return m_p2pInfo;
    }
    return m_p2pInfo;
}

void Service::updateUnconnectedEndpointToWservice()
{
    auto reconnectedPeers = std::make_shared<boostssl::ws::EndPoints>();
    RecursiveGuard l(x_nodes);
    for (auto const& it : m_staticNodes)
    {
        // p2pID is a empty string means that NodeIPEndpoint is unconnecnted, so update those
        // unconnecnted NodeIPEndpoints to wsService and wsService can reconnect them.
        if (it.second == "")
        {
            reconnectedPeers->insert(it.first);
        }
    }

    if (!reconnectedPeers->empty())
    {
        m_wsService->setReconnectedPeers(reconnectedPeers);
    }
}

void Service::obtainHostAndPortFromString(
    std::string const& _endpointString, std::string& host, uint16_t& port)
{
    std::vector<std::string> endpoint_info_vec;
    boost::split(
        endpoint_info_vec, _endpointString, boost::is_any_of(":"), boost::token_compress_on);
    if (!endpoint_info_vec.empty())
    {
        host = endpoint_info_vec[0];
    }
    if (endpoint_info_vec.size() > 1)
    {
        port = boost::lexical_cast<uint16_t>(endpoint_info_vec[1]);
    }

    SERVICE_LOG(INFO) << "obtainHostAndPortFromString " << LOG_KV("host", host)
                      << LOG_KV("port", port);
}

/// update the staticNodes
void Service::updateStaticNodes(std::string const& _endPoint, P2pID const& p2pID)
{
    std::string host;
    std::uint16_t port;
    obtainHostAndPortFromString(_endPoint, host, port);
    auto nodeIPEndpoint = NodeIPEndpoint(host, port);

    RecursiveGuard l(x_nodes);
    auto it = m_staticNodes.find(nodeIPEndpoint);
    // modify m_staticNodes(including accept cases, namely the client endpoint)
    if (it != m_staticNodes.end())
    {
        SERVICE_LOG(INFO) << LOG_DESC("updateStaticNodes") << LOG_KV("nodeid", p2pID)
                          << LOG_KV("endpoint", _endPoint);
        it->second = p2pID;

        updateUnconnectedEndpointToWservice();
    }
    else
    {
        SERVICE_LOG(DEBUG) << LOG_DESC("updateStaticNodes can't find endpoint")
                           << LOG_KV("p2pid", p2pID) << LOG_KV("endpoint", _endPoint);
    }
}

void Service::onConnect(std::shared_ptr<WsSession> _session)
{
    auto session = std::dynamic_pointer_cast<P2PSession>(_session);
    P2pID nodeid = session->nodeId();

    std::string peer = "unknown";
    if (session)
    {
        peer = session->endPoint();
    }
    SERVICE_LOG(INFO) << LOG_DESC("onConnect") << LOG_KV("p2pid", nodeid)
                      << LOG_KV("endpoint", peer);

    RecursiveGuard l(x_sessions);
    auto it = m_sessions.find(nodeid);
    if (it != m_sessions.end() && it->second->isConnected())
    {
        SERVICE_LOG(INFO) << "Disconnect duplicate peer" << LOG_KV("p2pid", nodeid);
        updateStaticNodes(session->endPoint(), nodeid);
        session->drop(DuplicatePeer);
        return;
    }

    if (nodeid == id())
    {
        SERVICE_LOG(TRACE) << "Disconnect self";
        updateStaticNodes(session->endPoint(), id());
        session->drop(DuplicatePeer);
        return;
    }

    session->initP2PInfo();
    session->setService(std::weak_ptr<Service>(shared_from_this()));
    session->start();
    asyncSendProtocol(_session);
    updateStaticNodes(session->endPoint(), nodeid);
    if (it != m_sessions.end())
    {
        it->second = session;
    }
    else
    {
        m_sessions.insert(std::make_pair(nodeid, session));
    }
    SERVICE_LOG(INFO) << LOG_DESC("Connection established") << LOG_KV("p2pid", nodeid)
                      << LOG_KV("endpoint", session->endPoint());
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
                           << LOG_KV("endpoint", p2pSession->endPoint());

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

    _p2pSession->asyncSendMessage(p2pMessage);

    SERVICE_LOG(TRACE) << "sendMessageBySession" << LOG_KV("seq", p2pMessage->seq())
                       << LOG_KV("packetType", _packetType)
                       << LOG_KV("p2pid", _p2pSession->nodeId())
                       << LOG_KV("payload.size()", _payload.size());
}

void Service::sendRespMessageBySession(bytesConstRef _payload,
    bcos::boostssl::MessageFace::Ptr _p2pMessage, P2PSession::Ptr _p2pSession)
{
    auto respMessage = std::static_pointer_cast<P2PMessage>(messageFactory()->buildMessage());

    respMessage->setSeq(_p2pMessage->seq());
    respMessage->setRespPacket();
    respMessage->setPayload(std::make_shared<bytes>(_payload.begin(), _payload.end()));

    _p2pSession->asyncSendMessage(respMessage);

    SERVICE_LOG(TRACE) << "sendRespMessageBySession" << LOG_KV("seq", _p2pMessage->seq())
                       << LOG_KV("p2pid", _p2pSession->nodeId())
                       << LOG_KV("payload size", _payload.size());
}

void Service::onMessage(NetworkException e, boostssl::ws::WsSession::Ptr session,
    bcos::boostssl::MessageFace::Ptr message, std::weak_ptr<P2PSession> p2pSessionWeakPtr)
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
            std::string host;
            uint16_t port;
            obtainHostAndPortFromString(session->endPoint(), host, port);
            NodeIPEndpoint nodeIPEndpoint(host, port);
        }

        if (e.errorCode())
        {
            SERVICE_LOG(WARNING) << LOG_DESC("disconnect error P2PSession")
                                 << LOG_KV("p2pid", p2pID) << LOG_KV("endpoint", nodeIPEndpoint)
                                 << LOG_KV("errorCode", e.errorCode())
                                 << LOG_KV("errorMessage", e.what());

            if (p2pSession)
            {
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
            handler(p2pMessage, p2pSession);
            return;
        }
        switch (packetType)
        {
        case GatewayMessageType::Heartbeat:
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

bcos::boostssl::MessageFace::Ptr Service::sendMessageByNodeID(
    P2pID p2pID, bcos::boostssl::MessageFace::Ptr message)
{
    try
    {
        struct SessionCallback : public std::enable_shared_from_this<SessionCallback>
        {
        public:
            using Ptr = std::shared_ptr<SessionCallback>;

            SessionCallback() { mutex.lock(); }

            void onResponse(NetworkException _error, std::shared_ptr<P2PSession>,
                bcos::boostssl::MessageFace::Ptr _message)
            {
                error = _error;
                response = _message;
                mutex.unlock();
            }

            NetworkException error;
            bcos::boostssl::MessageFace::Ptr response;
            std::mutex mutex;
        };

        SessionCallback::Ptr callback = std::make_shared<SessionCallback>();
        CallbackFuncWithSession fp = std::bind(&SessionCallback::onResponse, callback,
            std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
        asyncSendMessageByNodeID(p2pID, message, fp, boostssl::ws::Options());
        // lock to wait for async send
        callback->mutex.lock();
        callback->mutex.unlock();
        SERVICE_LOG(DEBUG) << LOG_DESC("sendMessageByNodeID mutex unlock");

        NetworkException error = callback->error;
        if (error.errorCode() != 0)
        {
            SERVICE_LOG(ERROR) << LOG_DESC("asyncSendMessageByNodeID error")
                               << LOG_KV("nodeid", p2pID) << LOG_KV("errorCode", error.errorCode())
                               << LOG_KV("what", error.what());
            BOOST_THROW_EXCEPTION(error);
        }

        return callback->response;
    }
    catch (std::exception& e)
    {
        SERVICE_LOG(ERROR) << LOG_DESC("asyncSendMessageByNodeID error") << LOG_KV("nodeid", p2pID)
                           << LOG_KV("what", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(e);
    }

    return P2PMessage::Ptr();
}

bool Service::connected(std::string const& _nodeID)
{
    RecursiveGuard l(x_sessions);
    auto it = m_sessions.find(_nodeID);
    return (it != m_sessions.end() && it->second->isConnected());
}

void Service::asyncSendMessageByNodeID(P2pID p2pID, bcos::boostssl::MessageFace::Ptr message,
    CallbackFuncWithSession callback, boostssl::ws::Options options)
{
    try
    {
        if (p2pID == id())
        {
            // ignore myself
            return;
        }

        RecursiveGuard l(x_sessions);
        auto it = m_sessions.find(p2pID);

        if (it != m_sessions.end() && it->second->isConnected())
        {
            if (message->seq() == "0")
            {
                message->setSeq(messageFactory()->newSeq());
            }
            auto session = it->second;
            if (callback)
            {
                session->asyncSendMessage(message, options,
                    [session, callback](bcos::Error::Ptr error, boostssl::MessageFace::Ptr message,
                        std::shared_ptr<WsSession>) {
                        P2PMessage::Ptr p2pMessage = std::dynamic_pointer_cast<P2PMessage>(message);
                        if (callback)
                        {
                            NetworkException e(error);
                            callback(e, session, p2pMessage);
                        }
                    });
            }
            else
            {
                session->asyncSendMessage(message, options, nullptr);
            }
        }
        else
        {
            if (callback)
            {
                NetworkException e(-1, "send message failed for no network established");
                callback(e, nullptr, nullptr);
            }
            SERVICE_LOG(WARNING) << "Node inactived" << LOG_KV("nodeid", p2pID);
        }
    }
    catch (std::exception& e)
    {
        SERVICE_LOG(ERROR) << "asyncSendMessageByNodeID" << LOG_KV("nodeid", p2pID)
                           << LOG_KV("what", boost::diagnostic_information(e));

        if (callback)
        {
            m_wsService->threadPool()->enqueue([callback, e] {
                callback(NetworkException(Disconnect, "Disconnect"), P2PSession::Ptr(),
                    P2PMessage::Ptr());
            });
        }
    }
}

void Service::asyncBroadcastMessage(
    bcos::boostssl::MessageFace::Ptr message, boostssl::ws::Options options)
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

P2pInfos Service::sessionInfos()
{
    P2pInfos infos;
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

bool Service::isConnected(P2pID const& p2pID) const
{
    RecursiveGuard l(x_sessions);
    auto it = m_sessions.find(p2pID);

    if (it != m_sessions.end() && it->second->isConnected())
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
    boostssl::ws::Options _options, P2PResponseCallback _callback)
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
            std::shared_ptr<boostssl::MessageFace> _p2pMessage) {
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
    int16_t _type, bytesConstRef _payload, boostssl::ws::Options _options)
{
    auto p2pMessage = newP2PMessage(_type, _payload);
    asyncBroadcastMessage(p2pMessage, _options);
}

void Service::asyncSendMessageByP2PNodeIDs(int16_t _type, const std::vector<P2pID>& _nodeIDs,
    bytesConstRef _payload, boostssl::ws::Options _options)
{
    for (auto const& p2pID : _nodeIDs)
    {
        asyncSendMessageByP2PNodeID(_type, p2pID, _payload, _options, nullptr);
    }
}

// send the protocolInfo
void Service::asyncSendProtocol(WsSession::Ptr _session)
{
    auto payload = std::make_shared<bytes>();
    m_codec->encode(m_localProtocol, *payload);
    auto message = std::static_pointer_cast<P2PMessage>(messageFactory()->buildMessage());
    message->setPacketType(GatewayMessageType::Handshake);
    auto seq = messageFactory()->newSeq();
    message->setSeq(seq);
    message->setPayload(payload);

    SERVICE_LOG(INFO) << LOG_DESC("asyncSendProtocol") << LOG_KV("payload", payload->size())
                      << LOG_KV("seq", seq);
    _session->asyncSendMessage(message, Options(), nullptr);
}

// receive the protocolInfo
void Service::onReceiveProtocol(
    boostssl::MessageFace::Ptr _message, std::shared_ptr<P2PSession> _session)
{
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
            _session->drop(DisconnectReason::NegotiateFailed);
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
