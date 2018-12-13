/*
    This file is part of FISCO-BCOS.

    FISCO-BCOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FISCO-BCOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>.
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

ChannelRPCServer::~ChannelRPCServer()
{
    StopListening();
}

bool ChannelRPCServer::StartListening()
{
    if (!_running)
    {
        CHANNEL_LOG(TRACE) << "Start ChannelRPCServer: " << _listenAddr << ":" << _listenPort;

        std::function<void(dev::channel::ChannelException, dev::channel::ChannelSession::Ptr)> fp =
            std::bind(&ChannelRPCServer::onConnect, shared_from_this(), std::placeholders::_1,
                std::placeholders::_2);
        _server->setConnectionHandler(fp);

        _server->run();

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
        CHANNEL_LOG(INFO) << "send ethereum resp seq:" << it->first << " response:" << _response;

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
        CHANNEL_LOG(ERROR) << "not found seq, may be timeout:" << addInfo;
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
        CHANNEL_LOG(ERROR) << "connection error: " << e.errorCode() << ", " << e.what();
    }
}

void ChannelRPCServer::onDisconnect(
    dev::channel::ChannelException e, dev::channel::ChannelSession::Ptr session)
{
    CHANNEL_LOG(ERROR) << "remove session: " << session->host() << ":" << session->port()
                       << " success";

    {
        std::lock_guard<std::mutex> lockSession(_sessionMutex);
        std::lock_guard<std::mutex> lockSeqMutex(_seqMutex);
        std::lock_guard<std::mutex> lockSeqMessageMutex(_seqMessageMutex);

        for (auto it : _sessions)
        {
            if (it.second == session)
            {
                auto c = _sessions.erase(it.first);
                CHANNEL_LOG(DEBUG) << "sessions removed: " << c;
                break;
            }
        }

        for (auto it : _seq2session)
        {
            if (it.second == session)
            {
                auto c = _seq2session.erase(it.first);
                CHANNEL_LOG(DEBUG) << "seq2session removed: " << c;
                break;
            }
        }

        for (auto it : _seq2MessageSession)
        {
            if (it.second.fromSession == session || it.second.toSession == session)
            {
                auto c = _seq2MessageSession.erase(it.first);
                CHANNEL_LOG(DEBUG) << "seq2MessageSession removed: " << c;
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
        CHANNEL_LOG(INFO) << "receive sdk message length:" << message->length()
                          << " type:" << message->type() << " sessionID:" << message->seq();

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
            CHANNEL_LOG(ERROR) << "unknown client message: " << message->type();
            break;
        }
    }
    else
    {
        CHANNEL_LOG(ERROR) << "error: " << e.errorCode() << ", " << e.what();

        onDisconnect(dev::channel::ChannelException(), session);
    }
}

void dev::ChannelRPCServer::onClientEthereumRequest(
    dev::channel::ChannelSession::Ptr session, dev::channel::Message::Ptr message)
{
    // CHANNEL_LOG(DEBUG) << "receive client ethereum request";

    std::string body(message->data(), message->data() + message->dataSize());

    CHANNEL_LOG(DEBUG) << "client ethereum request seq:" << message->seq() << "  ethereum request:"
                       << std::string((char*)message->data(), message->dataSize());

    {
        std::lock_guard<std::mutex> lock(_seqMutex);
        _seq2session.insert(std::make_pair(message->seq(), session));
    }

    std::string* addInfo = new std::string(message->seq());

    OnRequest(body, addInfo);
    // TODO:txpool regist callback
}

void dev::ChannelRPCServer::onReceiveChannelMessage(
    p2p::NetworkException, std::shared_ptr<p2p::P2PSession> s, p2p::P2PMessage::Ptr msg)
{
    uint32_t topicLen = ntohl(*((uint32_t*)msg->buffer()->data()));
    auto data = make_shared<bytes>(msg->buffer()->begin() + 4 + topicLen, msg->buffer()->end());
    onNodeRequest(s->nodeID(), data);
}

void dev::ChannelRPCServer::onClientTopicRequest(
    dev::channel::ChannelSession::Ptr session, dev::channel::Message::Ptr message)
{
    // CHANNEL_LOG(DEBUG) << "receive SDK topic message";

    std::string body(message->data(), message->data() + message->dataSize());

    CHANNEL_LOG(DEBUG) << "SDK topic message seq:" << message->seq() << "  topic message:" << body;

    try
    {
        std::stringstream ss;
        ss << body;

        Json::Value root;
        ss >> root;
        std::function<void(dev::p2p::NetworkException, std::shared_ptr<dev::p2p::P2PSession>,
            p2p::P2PMessage::Ptr)>
            fp = std::bind(&ChannelRPCServer::onReceiveChannelMessage, shared_from_this(),
                std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
        std::shared_ptr<std::set<std::string> > topics = std::make_shared<std::set<std::string> >();
        Json::Value topicsValue = root;
        if (!topicsValue.empty())
        {
            for (size_t i = 0; i < topicsValue.size(); ++i)
            {
                std::string topic = topicsValue[(int)i].asString();

                CHANNEL_LOG(DEBUG) << "topic:" << topic;

                topics->insert(topic);
                m_service->registerHandlerByTopic(topic, fp);
            }
        }

        session->setTopics(topics);

        updateHostTopics();
    }
    catch (exception& e)
    {
        CHANNEL_LOG(ERROR) << "ERROR:" << e.what();
    }
}

void dev::ChannelRPCServer::onClientChannelRequest(
    dev::channel::ChannelSession::Ptr session, dev::channel::Message::Ptr message)
{
    CHANNEL_LOG(DEBUG) << "SDK channel2 request";

    if (message->dataSize() < 1)
    {
        CHANNEL_LOG(ERROR) << "invalid channel2 message, too short:" << message->dataSize();
        return;
    }

    uint8_t topicLen = *((uint8_t*)message->data());
    std::string topic((char*)message->data() + 1, topicLen - 1);

    CHANNEL_LOG(DEBUG) << "target topic:" << topic;

    std::lock_guard<std::mutex> lock(_seqMessageMutex);
    auto it = _seq2MessageSession.find(message->seq());

    if (message->type() == 0x30)
    {
        try
        {
            CHANNEL_LOG(DEBUG) << "channel2 request:" << message->seq();

            ChannelMessageSession messageSession;
            messageSession.fromSession = session;
            messageSession.message = message;

            auto newIt = _seq2MessageSession.insert(std::make_pair(message->seq(), messageSession));
            if (newIt.second == false)
            {
                CHANNEL_LOG(WARNING) << "seq:" << message->seq() << " session repeat, overwrite";

                newIt.first->second.fromSession = session;
            }
            it = newIt.first;

            CHANNEL_LOG(DEBUG) << "send to node:" << it->second.message->seq();
            it->second.failedNodeIDs.insert(it->second.toNodeID);
            auto buffer = std::make_shared<bytes>();
            it->second.message->encode(*buffer);
            auto msg = std::dynamic_pointer_cast<p2p::P2PMessage>(
                m_service->p2pMessageFactory()->buildMessage());
            msg->setBuffer(buffer);
            msg->setProtocolID(dev::eth::ProtocolID::Topic);
            msg->setPacketType(0u);
            msg->setLength(p2p::P2PMessage::HEADER_LENGTH + msg->buffer()->size());

            m_service->sendMessageByTopic(topic, msg);
        }
        catch (exception& e)
        {
            CHANNEL_LOG(ERROR) << "send error:" << e.what();

            message->setType(0x31);
            message->setResult(REMOTE_PEER_UNAVAILIBLE);
            message->clearData();

            it->second.fromSession->asyncSendMessage(
                message, dev::channel::ChannelSession::CallbackType(), 0);
        }
    }
    else if (message->type() == 0x31)
    {
        try
        {
            if (it == _seq2MessageSession.end())
            {
                CHANNEL_LOG(WARNING) << "not found seq, may timeout?";

                return;
            }

            if (message->result() != 0)
            {
                try
                {
                    CHANNEL_LOG(DEBUG)
                        << "seq" << message->seq() << " push to  " << it->second.toSession->host()
                        << ":" << it->second.toSession->port() << " failed:" << message->result();
                    it->second.failedSessions.insert(it->second.toSession);

                    auto session =
                        sendChannelMessageToSession(topic, message, it->second.failedSessions);

                    CHANNEL_LOG(DEBUG) << "try push to" << session->host() << ":" << session->port()
                                       << " failed:" << message->result();
                    it->second.toSession = session;
                }
                catch (exception& e)
                {
                    CHANNEL_LOG(ERROR) << "message push totaly failed:" << e.what();

                    message->setResult(REMOTE_CLIENT_PEER_UNAVAILBLE);
                    message->setType(0x31);
                    auto buffer = std::make_shared<bytes>();
                    message->encode(*buffer);
                    auto msg = std::dynamic_pointer_cast<p2p::P2PMessage>(
                        m_service->p2pMessageFactory()->buildMessage());
                    msg->setBuffer(buffer);
                    msg->setProtocolID(dev::eth::ProtocolID::Topic);
                    msg->setPacketType(0u);
                    msg->setLength(p2p::P2PMessage::HEADER_LENGTH + msg->buffer()->size());
                    m_service->sendMessageByNodeID(it->second.fromNodeID, msg);
                }
            }
            else
            {
                CHANNEL_LOG(DEBUG) << "from SDK channel2 response:" << message->seq();
                auto buffer = std::make_shared<bytes>();
                message->encode(*buffer);
                auto msg = std::dynamic_pointer_cast<p2p::P2PMessage>(
                    m_service->p2pMessageFactory()->buildMessage());
                msg->setBuffer(buffer);
                msg->setProtocolID(dev::eth::ProtocolID::Topic);
                msg->setPacketType(0u);
                msg->setLength(p2p::P2PMessage::HEADER_LENGTH + msg->buffer()->size());
                m_service->sendMessageByNodeID(it->second.fromNodeID, msg);

                CHANNEL_LOG(DEBUG) << "send message to node:" << it->second.fromNodeID;

                _seq2MessageSession.erase(it);
            }
        }
        catch (exception& e)
        {
            CHANNEL_LOG(ERROR) << "send response error:" << e.what();
        }
    }
    else
    {
        CHANNEL_LOG(ERROR) << "unknown message type:" << message->type();
    }
}

void dev::ChannelRPCServer::onNodeRequest(h512 nodeID, std::shared_ptr<dev::bytes> message)
{
    auto msg = _server->messageFactory()->buildMessage();
    ssize_t result = msg->decode(message->data(), message->size());

    if (result <= 0)
    {
        CHANNEL_LOG(ERROR) << "decode error:" << result << " package size:" << message->size();
        return;
    }

    CHANNEL_LOG(DEBUG) << "receive node mssage length:" << message->size()
                       << " type:" << msg->type() << " seq:" << msg->seq();

    switch (msg->type())
    {
    case 0x30:  // request
    case 0x31:  // response
        onNodeChannelRequest(nodeID, msg);
        break;
    default:
        break;
    }
}

void ChannelRPCServer::onNodeChannelRequest(h512 nodeID, dev::channel::Message::Ptr message)
{
    CHANNEL_LOG(DEBUG) << "receive from node:" << nodeID
                       << " chanel message size:" << message->dataSize() + 14;

    try
    {
        if (message->dataSize() < 1)
        {
            CHANNEL_LOG(ERROR) << "invalid channel message, too short:" << message->dataSize();
            return;
        }

        uint8_t topicLen = *((uint8_t*)message->data());
        std::string topic((char*)message->data() + 1, topicLen - 1);

        CHANNEL_LOG(DEBUG) << "target topic:" << topic;

        std::lock_guard<std::mutex> lock(_seqMessageMutex);
        auto it = _seq2MessageSession.find(message->seq());

        if (message->type() == 0x30)
        {
            try
            {
                if (it == _seq2MessageSession.end())
                {
                    CHANNEL_LOG(DEBUG) << "new channel message";

                    ChannelMessageSession messageSession;
                    messageSession.fromNodeID = nodeID;
                    messageSession.message = message;

                    auto newIt =
                        _seq2MessageSession.insert(std::make_pair(message->seq(), messageSession));
                    if (newIt.second == false)
                    {
                        CHANNEL_LOG(WARNING)
                            << "seq:" << message->seq() << " session repeat, overwrite";

                        newIt.first->second = messageSession;
                    }

                    it = newIt.first;
                }

                auto session =
                    sendChannelMessageToSession(topic, message, it->second.failedSessions);

                it->second.toSession = session;
            }
            catch (std::exception& e)
            {
                CHANNEL_LOG(ERROR) << "push message totaly failed:" << e.what();

                message->setResult(REMOTE_CLIENT_PEER_UNAVAILBLE);
                message->setType(0x31);

                auto buffer = std::make_shared<bytes>();
                message->encode(*buffer);
                auto msg = std::dynamic_pointer_cast<dev::p2p::P2PMessage>(
                    m_service->p2pMessageFactory()->buildMessage());
                msg->setBuffer(buffer);
                msg->setProtocolID(dev::eth::ProtocolID::Topic);
                msg->setPacketType(0u);
                msg->setLength(p2p::P2PMessage::HEADER_LENGTH + msg->buffer()->size());
                m_service->sendMessageByNodeID(it->second.fromNodeID, msg);
            }
        }
        else if (message->type() == 0x31)
        {
            if (it == _seq2MessageSession.end())
            {
                CHANNEL_LOG(ERROR) << "error, not found session:" << message->seq();
                return;
            }

            if (message->result() != 0)
            {
                CHANNEL_LOG(DEBUG) << "message:" << message->seq() << " send to node"
                                   << it->second.toNodeID << "failed:" << message->result();
                try
                {
                    it->second.failedNodeIDs.insert(it->second.toNodeID);
                    auto buffer = std::make_shared<bytes>();
                    it->second.message->encode(*buffer);
                    auto msg = std::dynamic_pointer_cast<p2p::P2PMessage>(
                        m_service->p2pMessageFactory()->buildMessage());
                    msg->setBuffer(buffer);
                    msg->setProtocolID(dev::eth::ProtocolID::Topic);
                    msg->setPacketType(0u);
                    msg->setLength(p2p::P2PMessage::HEADER_LENGTH + msg->buffer()->size());

                    m_service->sendMessageByTopic(topic, msg);
                }
                catch (std::exception& e)
                {
                    CHANNEL_LOG(ERROR) << "process node message failed:" << e.what();

                    message->setType(0x31);
                    message->setResult(REMOTE_PEER_UNAVAILIBLE);
                    message->clearData();

                    it->second.fromSession->asyncSendMessage(
                        message, dev::channel::ChannelSession::CallbackType(), 0);
                }
            }
            else
            {
                CHANNEL_LOG(DEBUG) << "response seq:" << message->seq();

                if (it->second.fromSession->actived())
                {
                    it->second.fromSession->asyncSendMessage(
                        message, dev::channel::ChannelSession::CallbackType(), 0);

                    CHANNEL_LOG(DEBUG) << "response to seq[" << it->first << "] ["
                                       << it->second.fromSession->host() << ":"
                                       << it->second.fromSession->port() << "]success";
                }

                _seq2MessageSession.erase(it);
            }
        }
    }
    catch (std::exception& e)
    {
        CHANNEL_LOG(ERROR) << "ERROR:" << e.what();
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
                        CHANNEL_LOG(ERROR) << "ERROR:" << e.errorCode() << " message:" << e.what();

                        _exclude.insert(_currentSession);

                        sendMessage();

                        return;
                    }
                }
                catch (dev::channel::ChannelException& e)
                {
                    CHANNEL_LOG(ERROR) << "ERROR:" << e.errorCode() << " " << e.what();

                    try
                    {
                        _callback(e, dev::channel::Message::Ptr());
                    }
                    catch (exception& e)
                    {
                        CHANNEL_LOG(ERROR) << "ERROR" << e.what();
                    }
                }

                try
                {
                    _callback(e, message);
                }
                catch (exception& e)
                {
                    CHANNEL_LOG(ERROR) << "ERROR" << e.what();
                }
            }

            void sendMessage()
            {
                std::vector<dev::channel::ChannelSession::Ptr> activedSessions =
                    _server->getSessionByTopic(_topic);

                if (activedSessions.empty())
                {
                    CHANNEL_LOG(ERROR) << "no session use topic:" << _topic;
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
                CHANNEL_LOG(DEBUG) << "random node:" << ri;

                auto session = activedSessions[ri];

                std::function<void(dev::channel::ChannelException, dev::channel::Message::Ptr)> fp =
                    std::bind(&Callback::onResponse, shared_from_this(), std::placeholders::_1,
                        std::placeholders::_2);
                session->asyncSendMessage(_message, fp, 5000);

                CHANNEL_LOG(INFO) << "push to session: " << session->host() << ":"
                                  << session->port() << " success";
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
    catch (exception& e)
    {
        CHANNEL_LOG(ERROR) << "ERROR:" << e.what();
    }
}

dev::channel::TopicChannelMessage::Ptr ChannelRPCServer::pushChannelMessage(
    dev::channel::TopicChannelMessage::Ptr message)
{
    try
    {
        std::string topic = message->topic();

        CHANNEL_LOG(DEBUG) << "push to SDK:" << message->seq();
        std::vector<dev::channel::ChannelSession::Ptr> activedSessions = getSessionByTopic(topic);

        if (activedSessions.empty())
        {
            CHANNEL_LOG(ERROR) << "no SDK follow topic:" << topic;

            throw dev::channel::ChannelException(103, "send failed, no node follow topic:" + topic);
        }

        dev::channel::TopicChannelMessage::Ptr response;
        for (auto it : activedSessions)
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
        CHANNEL_LOG(ERROR) << "ERROR:" << e.what();

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

dev::channel::ChannelSession::Ptr ChannelRPCServer::sendChannelMessageToSession(std::string topic,
    dev::channel::Message::Ptr message, const std::set<dev::channel::ChannelSession::Ptr>& exclude)
{
    std::vector<dev::channel::ChannelSession::Ptr> activedSessions = getSessionByTopic(topic);

    for (auto sessionIt = activedSessions.begin(); sessionIt != activedSessions.end();)
    {
        if (exclude.find(*sessionIt) != exclude.end())
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
        CHANNEL_LOG(ERROR) << "no session follow topic:" << topic;
        throw dev::channel::ChannelException(104, "no session follow topic:" + topic);
    }

    boost::mt19937 rng(static_cast<unsigned>(std::time(0)));
    boost::uniform_int<int> index(0, activedSessions.size() - 1);

    auto ri = index(rng);
    CHANNEL_LOG(DEBUG) << "random node:" << ri;

    auto session = activedSessions[ri];

    session->asyncSendMessage(message, dev::channel::ChannelSession::CallbackType(), 0);

    CHANNEL_LOG(DEBUG) << "push to session: " << session->host() << ":" << session->port()
                       << " success";

    return session;
}

void ChannelRPCServer::updateHostTopics()
{
    auto allTopics = std::make_shared<std::vector<std::string> >();

    std::lock_guard<std::mutex> lock(_sessionMutex);
    for (auto it : _sessions)
    {
        auto topics = it.second->topics();
        allTopics->insert(allTopics->end(), topics->begin(), topics->end());
    }

    m_service->setTopics(allTopics);
}

std::vector<dev::channel::ChannelSession::Ptr> ChannelRPCServer::getSessionByTopic(
    const std::string& topic)
{
    std::vector<dev::channel::ChannelSession::Ptr> activedSessions;

    std::lock_guard<std::mutex> lock(_sessionMutex);
    for (auto it : _sessions)
    {
        if (it.second->topics()->empty() || !it.second->actived())
        {
            continue;
        }

        auto topicIt = it.second->topics()->find(topic);
        if (topicIt != it.second->topics()->end())
        {
            activedSessions.push_back(it.second);
        }
    }

    return activedSessions;
}
