/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */
/**
 * @file: ChannelRPCServer.cpp
 * @author: monan
 * @date: 2017
 *
 * @author xingqiangbai
 * @date 2018.11.5
 * @modify use libp2p to send message
 */

#include "ChannelRPCServer.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <json/json.h>
#include <libdevcore/easylog.h>
#include <libp2p/P2PMessage.h>
#include <libp2p/Service.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <boost/algorithm/string/classification.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/random.hpp>
#include <boost/range/algorithm/remove_if.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace dev::channel;

static const int c_seqAbridgedLen = 8;

ChannelRPCServer::~ChannelRPCServer()
{
    StopListening();
}

bool ChannelRPCServer::StartListening()
{
    if (!_running)
    {
        CHANNEL_LOG(TRACE) << "Start ChannelRPCServer" << LOG_KV("Host", _listenAddr) << ":"
                           << _listenPort;

        std::function<void(dev::channel::ChannelException, dev::channel::ChannelSession::Ptr)> fp =
            std::bind(&ChannelRPCServer::onConnect, shared_from_this(), std::placeholders::_1,
                std::placeholders::_2);
        _server->setConnectionHandler(fp);

        _server->run();

        std::function<void(dev::network::NetworkException, std::shared_ptr<dev::p2p::P2PSession>,
            p2p::P2PMessage::Ptr)>
            channelHandler = std::bind(&ChannelRPCServer::onNodeChannelRequest, shared_from_this(),
                std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

        m_service->registerHandlerByProtoclID(dev::eth::ProtocolID::AMOP, channelHandler);

        CHANNEL_LOG(INFO) << "ChannelRPCServer started";

        _running = true;
    }

    return true;
}

void ChannelRPCServer::initSSLContext()
{
    _sslContext = std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tlsv12);

    vector<pair<string, Public> > certificates;
    string nodepri;

    EC_KEY* ecdh = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    SSL_CTX_set_tmp_ecdh(_sslContext->native_handle(), ecdh);
    EC_KEY_free(ecdh);

    _sslContext->set_verify_depth(3);
    _sslContext->add_certificate_authority(
        boost::asio::const_buffer(certificates[0].first.c_str(), certificates[0].first.size()));

    string chain = certificates[0].first + certificates[1].first;
    _sslContext->use_certificate_chain(boost::asio::const_buffer(chain.c_str(), chain.size()));
    _sslContext->use_certificate(
        boost::asio::const_buffer(certificates[2].first.c_str(), certificates[2].first.size()),
        ba::ssl::context::file_format::pem);

    _sslContext->use_private_key(
        boost::asio::const_buffer(nodepri.c_str(), nodepri.size()), ba::ssl::context_base::pem);

    _server = make_shared<dev::channel::ChannelServer>();
    _server->setIOService(_ioService);
    _server->setSSLContext(_sslContext);

    _server->setEnableSSL(true);
    _server->setBind(_listenAddr, _listenPort);

    std::function<void(dev::channel::ChannelException, dev::channel::ChannelSession::Ptr)> fp =
        std::bind(&ChannelRPCServer::onConnect, shared_from_this(), std::placeholders::_1,
            std::placeholders::_2);
    _server->setConnectionHandler(fp);
}

bool ChannelRPCServer::StopListening()
{
    if (_running)
    {
        _server->stop();
    }

    _running = false;

    return true;
}

bool ChannelRPCServer::SendResponse(const std::string& _response, void* _addInfo)
{
    std::string addInfo = *((std::string*)_addInfo);

    std::lock_guard<std::mutex> lock(_seqMutex);
    auto it = _seq2session.find(addInfo);

    delete (std::string*)_addInfo;

    if (it != _seq2session.end())
    {
        CHANNEL_LOG(DEBUG) << "send ethereum resp seq"
                           << LOG_KV("seq", it->first.substr(0, c_seqAbridgedLen))
                           << LOG_KV("response", _response);

        std::shared_ptr<bytes> resp(new bytes());

        auto message = it->second->messageFactory()->buildMessage();
        message->setSeq(it->first);
        message->setResult(0);
        message->setType(0x12);
        message->setData((const byte*)_response.data(), _response.size());

        it->second->asyncSendMessage(message, dev::channel::ChannelSession::CallbackType(), 0);

        _seq2session.erase(it);
    }
    else
    {
        CHANNEL_LOG(ERROR) << "not found seq may be timeout";
    }

    return false;
}

void dev::ChannelRPCServer::removeSession(int sessionID)
{
    std::lock_guard<std::mutex> lock(_sessionMutex);
    auto it = _sessions.find(sessionID);

    if (it != _sessions.end())
    {
        _sessions.erase(it);
    }
}

void ChannelRPCServer::onConnect(
    dev::channel::ChannelException e, dev::channel::ChannelSession::Ptr session)
{
    if (e.errorCode() == 0)
    {
        CHANNEL_LOG(INFO) << "channel new connect";

        auto sessionID = ++_sessionCount;
        {
            std::lock_guard<std::mutex> lock(_sessionMutex);
            _sessions.insert(std::make_pair(sessionID, session));
        }

        std::function<void(dev::channel::ChannelSession::Ptr, dev::channel::ChannelException,
            dev::channel::Message::Ptr)>
            fp = std::bind(&dev::ChannelRPCServer::onClientRequest, this, std::placeholders::_1,
                std::placeholders::_2, std::placeholders::_3);
        session->setMessageHandler(fp);

        session->run();
        CHANNEL_LOG(INFO) << "start receive";
    }
    else
    {
        CHANNEL_LOG(ERROR) << "onConnect error" << LOG_KV("errorCode", e.errorCode())
                           << LOG_KV("what", boost::diagnostic_information(e));
    }
}

void ChannelRPCServer::onDisconnect(
    dev::channel::ChannelException, dev::channel::ChannelSession::Ptr session)
{
    CHANNEL_LOG(DEBUG) << "onDisconnect remove session" << LOG_KV("From", session->host()) << ":"
                       << session->port();

    {
        std::lock_guard<std::mutex> lockSession(_sessionMutex);
        std::lock_guard<std::mutex> lockSeqMutex(_seqMutex);

        for (auto& it : _sessions)
        {
            if (it.second == session)
            {
                _sessions.erase(it.first);
                CHANNEL_LOG(DEBUG) << "sessions removed";
                break;
            }
        }

        for (auto& it : _seq2session)
        {
            if (it.second == session)
            {
                _seq2session.erase(it.first);
                CHANNEL_LOG(DEBUG) << "seq2session removed";
                break;
            }
        }
    }

    updateHostTopics();
}

void dev::ChannelRPCServer::onClientRequest(dev::channel::ChannelSession::Ptr session,
    dev::channel::ChannelException e, dev::channel::Message::Ptr message)
{
    if (e.errorCode() == 0)
    {
        CHANNEL_LOG(INFO) << "receive sdk message" << LOG_KV("length", message->length())
                          << LOG_KV("type", message->type())
                          << LOG_KV("seq", message->seq().substr(0, c_seqAbridgedLen));

        switch (message->type())
        {
        case 0x12:
            onClientEthereumRequest(session, message);
            break;
        case 0x13:
        {
            std::string data((char*)message->data(), message->dataSize());

            if (data == "0")
            {
                data = "1";
                message->setData((const byte*)data.data(), data.size());
                session->asyncSendMessage(message, dev::channel::ChannelSession::CallbackType(), 0);
            }
            break;
        }
        case 0x30:
        case 0x31:
            onClientChannelRequest(session, message);
            break;
        case 0x32:
            onClientTopicRequest(session, message);
            break;
        default:
            CHANNEL_LOG(ERROR) << "unknown client message" << LOG_KV("type", message->type());
            break;
        }
    }
    else
    {
        CHANNEL_LOG(WARNING) << "onClientRequest" << LOG_KV("errorCode", e.errorCode())
                             << LOG_KV("what", e.what());

        onDisconnect(dev::channel::ChannelException(), session);
    }
}

void dev::ChannelRPCServer::onClientEthereumRequest(
    dev::channel::ChannelSession::Ptr session, dev::channel::Message::Ptr message)
{
    // CHANNEL_LOG(DEBUG) << "receive client ethereum request";

    std::string body(message->data(), message->data() + message->dataSize());

    CHANNEL_LOG(DEBUG) << "client ethereum request"
                       << LOG_KV("seq", message->seq().substr(0, c_seqAbridgedLen))
                       << LOG_KV("ethereum request",
                              std::string((char*)message->data(), message->dataSize()));

    {
        std::lock_guard<std::mutex> lock(_seqMutex);
        _seq2session.insert(std::make_pair(message->seq(), session));

        if (m_callbackSetter)
        {
            auto seq = message->seq();
            auto sessionRef = std::weak_ptr<dev::channel::ChannelSession>(session);
            auto serverRef = std::weak_ptr<dev::channel::ChannelServer>(_server);

            m_callbackSetter(new std::function<void(const std::string& receiptContext)>(
                [serverRef, sessionRef, seq](const std::string& receiptContext) {
                    auto server = serverRef.lock();
                    auto session = sessionRef.lock();
                    if (server && session)
                    {
                        auto channelMessage = server->messageFactory()->buildMessage();
                        channelMessage->setType(0x1000);
                        channelMessage->setSeq(seq);
                        channelMessage->setResult(0);
                        channelMessage->setData(
                            (const byte*)receiptContext.c_str(), receiptContext.size());

                        LOG(TRACE) << "Push transaction notify: " << seq;
                        session->asyncSendMessage(channelMessage,
                            std::function<void(dev::channel::ChannelException, Message::Ptr)>(), 0);
                    }
                }));
        }
    }

    std::string* addInfo = new std::string(message->seq());

    try
    {
        OnRequest(body, addInfo);
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "Error while onRequest rpc: " << boost::diagnostic_information(e);
    }

    if (m_callbackSetter)
    {
        m_callbackSetter(NULL);
    }
}

void dev::ChannelRPCServer::onNodeChannelRequest(
    dev::network::NetworkException, std::shared_ptr<p2p::P2PSession> s, p2p::P2PMessage::Ptr msg)
{
    auto channelMessage = _server->messageFactory()->buildMessage();
    ssize_t result = channelMessage->decode(msg->buffer()->data(), msg->buffer()->size());

    if (result <= 0)
    {
        CHANNEL_LOG(ERROR) << "onNodeChannelRequest decode error"
                           << LOG_KV(" package size", msg->buffer()->size());
        return;
    }

    CHANNEL_LOG(DEBUG) << "receive node request" << LOG_KV("from", s->nodeID())
                       << LOG_KV("length", msg->buffer()->size())
                       << LOG_KV("type", channelMessage->type())
                       << LOG_KV("seq", channelMessage->seq());

    try
    {
        if (channelMessage->dataSize() < 1)
        {
            CHANNEL_LOG(ERROR) << "invalid channel message too short";
            return;
        }

        uint8_t topicLen = *((uint8_t*)channelMessage->data());
        std::string topic((char*)channelMessage->data() + 1, topicLen - 1);

        CHANNEL_LOG(DEBUG) << "onNodeChannelRequest target" << LOG_KV("topic", topic);

        if (channelMessage->type() == 0x30)
        {
            try
            {
                auto nodeID = s->nodeID();
                auto p2pMessage = msg;
                auto service = m_service;
                asyncPushChannelMessage(topic, channelMessage,
                    [nodeID, channelMessage, service, p2pMessage](
                        dev::channel::ChannelException e, dev::channel::Message::Ptr response) {
                        if (e.errorCode())
                        {
                            CHANNEL_LOG(ERROR) << "Push channel message failed"
                                               << LOG_KV("what", boost::diagnostic_information(e));

                            channelMessage->setResult(e.errorCode());
                            channelMessage->setType(0x31);

                            auto buffer = std::make_shared<bytes>();
                            channelMessage->encode(*buffer);
                            auto p2pResponse = std::dynamic_pointer_cast<dev::p2p::P2PMessage>(
                                service->p2pMessageFactory()->buildMessage());
                            p2pResponse->setBuffer(buffer);
                            p2pResponse->setProtocolID(-dev::eth::ProtocolID::AMOP);
                            p2pResponse->setPacketType(0u);
                            p2pResponse->setSeq(p2pMessage->seq());
                            p2pResponse->setLength(
                                p2p::P2PMessage::HEADER_LENGTH + p2pResponse->buffer()->size());
                            service->asyncSendMessageByNodeID(nodeID, p2pResponse,
                                CallbackFuncWithSession(), dev::network::Options());

                            return;
                        }

                        CHANNEL_LOG(TRACE)
                            << "Receive sdk response" << LOG_KV("seq", response->seq());
                        auto buffer = std::make_shared<bytes>();
                        response->encode(*buffer);
                        auto p2pResponse = std::dynamic_pointer_cast<dev::p2p::P2PMessage>(
                            service->p2pMessageFactory()->buildMessage());
                        p2pResponse->setBuffer(buffer);
                        p2pResponse->setProtocolID(-dev::eth::ProtocolID::AMOP);
                        p2pResponse->setPacketType(0u);
                        p2pResponse->setSeq(p2pMessage->seq());
                        p2pResponse->setLength(
                            p2p::P2PMessage::HEADER_LENGTH + p2pResponse->buffer()->size());
                        service->asyncSendMessageByNodeID(nodeID, p2pResponse,
                            CallbackFuncWithSession(), dev::network::Options());
                    });
            }
            catch (std::exception& e)
            {
                CHANNEL_LOG(ERROR) << "push message totaly failed"
                                   << LOG_KV("what", boost::diagnostic_information(e));

                channelMessage->setResult(REMOTE_CLIENT_PEER_UNAVAILBLE);
                channelMessage->setType(0x31);

                auto buffer = std::make_shared<bytes>();
                channelMessage->encode(*buffer);
                auto p2pResponse = std::dynamic_pointer_cast<dev::p2p::P2PMessage>(
                    m_service->p2pMessageFactory()->buildMessage());
                p2pResponse->setBuffer(buffer);
                p2pResponse->setProtocolID(dev::eth::ProtocolID::AMOP);
                p2pResponse->setPacketType(0u);
                p2pResponse->setSeq(msg->seq());
                p2pResponse->setLength(
                    p2p::P2PMessage::HEADER_LENGTH + p2pResponse->buffer()->size());
                m_service->asyncSendMessageByNodeID(
                    s->nodeID(), p2pResponse, CallbackFuncWithSession(), dev::network::Options());
            }
        }
    }
    catch (std::exception& e)
    {
        CHANNEL_LOG(ERROR) << "error" << LOG_KV("what", boost::diagnostic_information(e));
    }
}

void dev::ChannelRPCServer::onClientTopicRequest(
    dev::channel::ChannelSession::Ptr session, dev::channel::Message::Ptr message)
{
    // CHANNEL_LOG(DEBUG) << "receive SDK topic message";

    std::string body(message->data(), message->data() + message->dataSize());

    CHANNEL_LOG(DEBUG) << "SDK topic message"
                       << LOG_KV("seq", message->seq().substr(0, c_seqAbridgedLen))
                       << LOG_KV("message", body);

    try
    {
        std::stringstream ss;
        ss << body;

        Json::Value root;
        ss >> root;

        std::shared_ptr<std::set<std::string> > topics = std::make_shared<std::set<std::string> >();
        Json::Value topicsValue = root;
        if (!topicsValue.empty())
        {
            for (size_t i = 0; i < topicsValue.size(); ++i)
            {
                std::string topic = topicsValue[(int)i].asString();

                CHANNEL_LOG(TRACE) << "onClientTopicRequest insert" << LOG_KV("topic", topic);

                topics->insert(topic);
            }
        }

        session->setTopics(topics);

        updateHostTopics();
    }
    catch (exception& e)
    {
        CHANNEL_LOG(ERROR) << "onClientTopicRequest error"
                           << LOG_KV("what", boost::diagnostic_information(e));
    }
}

void dev::ChannelRPCServer::onClientChannelRequest(
    dev::channel::ChannelSession::Ptr session, dev::channel::Message::Ptr message)
{
    CHANNEL_LOG(DEBUG) << "SDK channel2 request";

    if (message->dataSize() < 1)
    {
        CHANNEL_LOG(ERROR) << "invalid channel2 message too short";
        return;
    }

    uint8_t topicLen = *((uint8_t*)message->data());
    std::string topic((char*)message->data() + 1, topicLen - 1);

    CHANNEL_LOG(DEBUG) << "target topic:" << topic;

    if (message->type() == 0x30)
    {
        try
        {
            CHANNEL_LOG(DEBUG) << "channel2 request"
                               << LOG_KV("seq", message->seq().substr(0, c_seqAbridgedLen));

            auto buffer = std::make_shared<bytes>();
            message->encode(*buffer);

            auto p2pMessage = std::dynamic_pointer_cast<p2p::P2PMessage>(
                m_service->p2pMessageFactory()->buildMessage());
            p2pMessage->setBuffer(buffer);
            p2pMessage->setProtocolID(dev::eth::ProtocolID::AMOP);
            p2pMessage->setPacketType(0u);
            p2pMessage->setLength(p2p::P2PMessage::HEADER_LENGTH + p2pMessage->buffer()->size());

            dev::network::Options options;
            options.timeout = 30 * 1000;  // 30 seconds

            m_service->asyncSendMessageByTopic(topic, p2pMessage,
                [session, message](dev::network::NetworkException e,
                    std::shared_ptr<dev::p2p::P2PSession>, dev::p2p::P2PMessage::Ptr response) {
                    if (e.errorCode())
                    {
                        LOG(ERROR) << "ChannelMessage failed" << LOG_KV("errorCode", e.errorCode())
                                   << LOG_KV("what", boost::diagnostic_information(e));
                        message->setType(0x31);
                        message->setResult(REMOTE_PEER_UNAVAILIBLE);
                        message->clearData();

                        session->asyncSendMessage(
                            message, dev::channel::ChannelSession::CallbackType(), 0);

                        return;
                    }

                    message->decode(response->buffer()->data(), response->buffer()->size());

                    session->asyncSendMessage(
                        message, dev::channel::ChannelSession::CallbackType(), 0);
                },
                options);
        }
        catch (exception& e)
        {
            CHANNEL_LOG(ERROR) << "send error" << LOG_KV("what", boost::diagnostic_information(e));

            message->setType(0x31);
            message->setResult(REMOTE_PEER_UNAVAILIBLE);
            message->clearData();

            session->asyncSendMessage(message, dev::channel::ChannelSession::CallbackType(), 0);
        }
    }
    else
    {
        CHANNEL_LOG(ERROR) << "unknown message type" << LOG_KV("type", message->type());
    }
}

void ChannelRPCServer::setListenAddr(const std::string& listenAddr)
{
    _listenAddr = listenAddr;
}

void ChannelRPCServer::setListenPort(int listenPort)
{
    _listenPort = listenPort;
}

void ChannelRPCServer::CloseConnection(int _socket)
{
    close(_socket);
}

void ChannelRPCServer::setService(std::shared_ptr<dev::p2p::P2PInterface> _service)
{
    m_service = _service;
}

void ChannelRPCServer::setSSLContext(std::shared_ptr<boost::asio::ssl::context> sslContext)
{
    _sslContext = sslContext;
}

void ChannelRPCServer::setChannelServer(std::shared_ptr<dev::channel::ChannelServer> server)
{
    _server = server;
}

void ChannelRPCServer::asyncPushChannelMessage(std::string topic,
    dev::channel::Message::Ptr message,
    std::function<void(dev::channel::ChannelException, dev::channel::Message::Ptr)> callback)
{
    try
    {
        class Callback : public std::enable_shared_from_this<Callback>
        {
        public:
            typedef std::shared_ptr<Callback> Ptr;

            Callback(std::string topic, dev::channel::Message::Ptr message,
                ChannelRPCServer::Ptr server,
                std::function<void(dev::channel::ChannelException, dev::channel::Message::Ptr)>
                    callback)
              : _topic(topic), _message(message), _server(server), _callback(callback){};

            void onResponse(dev::channel::ChannelException e, dev::channel::Message::Ptr message)
            {
                try
                {
                    if (e.errorCode() != 0)
                    {
                        CHANNEL_LOG(ERROR)
                            << "onResponse error" << LOG_KV("errorCode", e.errorCode())
                            << LOG_KV("what", boost::diagnostic_information(e));

                        _exclude.insert(_currentSession);

                        sendMessage();

                        return;
                    }
                }
                catch (dev::channel::ChannelException& ex)
                {
                    CHANNEL_LOG(ERROR) << "onResponse error" << LOG_KV("errorCode", ex.errorCode())
                                       << LOG_KV("what", ex.what());

                    try
                    {
                        _callback(ex, dev::channel::Message::Ptr());
                    }
                    catch (exception& e)
                    {
                        CHANNEL_LOG(ERROR) << "onResponse error"
                                           << LOG_KV("what", boost::diagnostic_information(e));
                    }

                    return;
                }

                try
                {
                    _callback(e, message);
                }
                catch (exception& e)
                {
                    CHANNEL_LOG(ERROR)
                        << "onResponse error" << LOG_KV("what", boost::diagnostic_information(e));
                }
            }

            void sendMessage()
            {
                std::vector<dev::channel::ChannelSession::Ptr> activedSessions =
                    _server->getSessionByTopic(_topic);

                if (activedSessions.empty())
                {
                    CHANNEL_LOG(TRACE) << "no session use topic" << LOG_KV("topic", _topic);
                    throw dev::channel::ChannelException(104, "no session use topic:" + _topic);
                }

                for (auto sessionIt = activedSessions.begin(); sessionIt != activedSessions.end();)
                {
                    if (_exclude.find(*sessionIt) != _exclude.end())
                    {
                        sessionIt = activedSessions.erase(sessionIt);
                    }
                    else
                    {
                        ++sessionIt;
                    }
                }

                if (activedSessions.empty())
                {
                    CHANNEL_LOG(ERROR) << "all session try failed";

                    throw dev::channel::ChannelException(104, "all session failed");
                }

                boost::mt19937 rng(static_cast<unsigned>(std::time(0)));
                boost::uniform_int<int> index(0, activedSessions.size() - 1);

                auto ri = index(rng);
                CHANNEL_LOG(TRACE) << "random node" << ri;

                auto session = activedSessions[ri];

                std::function<void(dev::channel::ChannelException, dev::channel::Message::Ptr)> fp =
                    std::bind(&Callback::onResponse, shared_from_this(), std::placeholders::_1,
                        std::placeholders::_2);
                session->asyncSendMessage(_message, fp, 5000);

                CHANNEL_LOG(INFO) << "Push channel message success"
                                  << LOG_KV("seq", _message->seq().substr(0, c_seqAbridgedLen))
                                  << LOG_KV("session", session->host()) << ":" << session->port();
                _currentSession = session;
            }

        private:
            std::string _topic;
            dev::channel::Message::Ptr _message;
            ChannelRPCServer::Ptr _server;
            dev::channel::ChannelSession::Ptr _currentSession;
            std::set<dev::channel::ChannelSession::Ptr> _exclude;
            std::function<void(dev::channel::ChannelException, dev::channel::Message::Ptr)>
                _callback;
        };

        Callback::Ptr pushCallback =
            std::make_shared<Callback>(topic, message, shared_from_this(), callback);
        pushCallback->sendMessage();
    }
    catch (dev::channel::ChannelException& ex)
    {
        callback(ex, dev::channel::Message::Ptr());
    }
    catch (exception& e)
    {
        CHANNEL_LOG(ERROR) << "asyncPushChannelMessage error"
                           << LOG_KV("what", boost::diagnostic_information(e));
    }
}

void ChannelRPCServer::asyncBroadcastChannelMessage(
    std::string topic, dev::channel::Message::Ptr message)
{
    std::vector<dev::channel::ChannelSession::Ptr> activedSessions = getSessionByTopic(topic);
    if (activedSessions.empty())
    {
        CHANNEL_LOG(TRACE) << "no session use topic" << LOG_KV("topic", topic);
        return;
    }

    for (auto session : activedSessions)
    {
        session->asyncSendMessage(
            message, std::function<void(dev::channel::ChannelException, Message::Ptr)>(), 0);

        CHANNEL_LOG(INFO) << "Push channel message success" << LOG_KV("topic", topic)
                          << LOG_KV("seq", message->seq().substr(0, c_seqAbridgedLen))
                          << LOG_KV("session", session->host()) << ":" << session->port();
    }
}

dev::channel::TopicChannelMessage::Ptr ChannelRPCServer::pushChannelMessage(
    dev::channel::TopicChannelMessage::Ptr message)
{
    try
    {
        std::string topic = message->topic();

        CHANNEL_LOG(DEBUG) << "Push to SDK" << LOG_KV("topic", topic)
                           << LOG_KV("seq", message->seq().substr(0, c_seqAbridgedLen));
        std::vector<dev::channel::ChannelSession::Ptr> activedSessions = getSessionByTopic(topic);

        if (activedSessions.empty())
        {
            CHANNEL_LOG(ERROR) << "no SDK follow topic" << LOG_KV("topic", topic);

            throw dev::channel::ChannelException(103, "send failed, no node follow topic:" + topic);
        }

        dev::channel::TopicChannelMessage::Ptr response;
        for (auto& it : activedSessions)
        {
            dev::channel::Message::Ptr responseMessage = it->sendMessage(message, 0);

            if (responseMessage.get() != NULL && responseMessage->result() == 0)
            {
                response = std::make_shared<TopicChannelMessage>(responseMessage.get());
                break;
            }
        }

        if (!response)
        {
            throw dev::channel::ChannelException(99, "send fail, all retry failed");
        }

        return response;
    }
    catch (dev::channel::ChannelException& e)
    {
        CHANNEL_LOG(ERROR) << "pushChannelMessage error"
                           << LOG_KV("what", boost::diagnostic_information(e));

        throw e;
    }

    return dev::channel::TopicChannelMessage::Ptr();
}

std::string ChannelRPCServer::newSeq()
{
    static boost::uuids::random_generator uuidGenerator;
    std::string s = to_string(uuidGenerator());
    s.erase(boost::remove_if(s, boost::is_any_of("-")), s.end());
    return s;
}

void ChannelRPCServer::updateHostTopics()
{
    auto allTopics = std::make_shared<std::vector<std::string> >();

    std::lock_guard<std::mutex> lock(_sessionMutex);
    for (auto& it : _sessions)
    {
        auto topics = it.second->topics();
        allTopics->insert(allTopics->end(), topics.begin(), topics.end());
    }

    m_service->setTopics(allTopics);
}

std::vector<dev::channel::ChannelSession::Ptr> ChannelRPCServer::getSessionByTopic(
    const std::string& topic)
{
    std::vector<dev::channel::ChannelSession::Ptr> activedSessions;

    std::lock_guard<std::mutex> lock(_sessionMutex);
    for (auto& it : _sessions)
    {
        if (it.second->topics().empty() || !it.second->actived())
        {
            continue;
        }

        auto topics = it.second->topics();
        auto topicIt = topics.find(topic);
        if (topicIt != topics.end())
        {
            activedSessions.push_back(it.second);
        }
    }

    return activedSessions;
}
