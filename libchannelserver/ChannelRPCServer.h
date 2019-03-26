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
 * @file: ChannelRPCServer.h
 * @author: monan
 * @date: 2017
 *
 * @author xingqiangbai
 * @date 2018.11.5
 * @modify use libp2p to send message
 */

#pragma once

#include "ChannelException.h"
#include "ChannelMessage.h"
#include "ChannelServer.h"
#include "ChannelSession.h"
#include "libdevcore/ThreadPool.h"
#include <jsonrpccpp/server/abstractserverconnector.h>
#include <libdevcore/FixedHash.h>
#include <libethcore/Common.h>
#include <libp2p/Service.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <boost/asio.hpp>
#include <queue>
#include <string>
#include <thread>

namespace dev
{
namespace p2p
{
class P2PInterface;
}

class ChannelRPCServer : public jsonrpc::AbstractServerConnector,
                         public std::enable_shared_from_this<ChannelRPCServer>
{
public:
    enum ChannelERRORCODE
    {
        REMOTE_PEER_UNAVAILIBLE = 100,
        REMOTE_CLIENT_PEER_UNAVAILBLE = 101,
        TIMEOUT = 102
    };

    typedef std::shared_ptr<ChannelRPCServer> Ptr;

    ChannelRPCServer(std::string listenAddr = "", int listenPort = 0)
      : jsonrpc::AbstractServerConnector(), _listenAddr(listenAddr), _listenPort(listenPort){};
    virtual ~ChannelRPCServer();
    virtual bool StartListening() override;
    virtual bool StopListening() override;
    virtual bool SendResponse(std::string const& _response, void* _addInfo = nullptr) override;


    virtual void onConnect(
        dev::channel::ChannelException e, dev::channel::ChannelSession::Ptr session);


    virtual void onDisconnect(
        dev::channel::ChannelException e, dev::channel::ChannelSession::Ptr session);


    virtual void onClientRequest(dev::channel::ChannelSession::Ptr session,
        dev::channel::ChannelException e, dev::channel::Message::Ptr message);

    virtual void onClientEthereumRequest(
        dev::channel::ChannelSession::Ptr session, dev::channel::Message::Ptr message);

    virtual void onClientTopicRequest(
        dev::channel::ChannelSession::Ptr session, dev::channel::Message::Ptr message);

    virtual void onClientChannelRequest(
        dev::channel::ChannelSession::Ptr session, dev::channel::Message::Ptr message);

    void setListenAddr(const std::string& listenAddr);

    void setListenPort(int listenPort);

    void removeSession(int sessionID);

    void CloseConnection(int _socket);

    void onNodeChannelRequest(dev::network::NetworkException, std::shared_ptr<p2p::P2PSession>,
        std::shared_ptr<p2p::P2PMessage>);

    void setService(std::shared_ptr<dev::p2p::P2PInterface> _service);

    void setSSLContext(std::shared_ptr<boost::asio::ssl::context> sslContext);

    std::shared_ptr<dev::channel::ChannelServer> channelServer() { return _server; }
    void setChannelServer(std::shared_ptr<dev::channel::ChannelServer> server);

    void asyncPushChannelMessage(std::string topic, dev::channel::Message::Ptr message,
        std::function<void(dev::channel::ChannelException, dev::channel::Message::Ptr)> callback);

    void asyncBroadcastChannelMessage(std::string topic, dev::channel::Message::Ptr message);

    virtual dev::channel::TopicChannelMessage::Ptr pushChannelMessage(
        dev::channel::TopicChannelMessage::Ptr message);

    virtual std::string newSeq();

    void setCallbackSetter(
        std::function<void(std::function<void(const std::string& receiptContext)>*)> callbackSetter)
    {
        m_callbackSetter = callbackSetter;
    };

    void addHandler(const dev::eth::Handler<int64_t>& handler) { m_handlers.push_back(handler); }

private:
    void initSSLContext();

    dev::channel::ChannelSession::Ptr sendChannelMessageToSession(std::string topic,
        dev::channel::Message::Ptr message,
        const std::set<dev::channel::ChannelSession::Ptr>& exclude);

    void updateHostTopics();

    std::vector<dev::channel::ChannelSession::Ptr> getSessionByTopic(const std::string& topic);

    bool _running = false;

    std::string _listenAddr;
    int _listenPort;
    std::shared_ptr<boost::asio::io_service> _ioService;

    std::shared_ptr<boost::asio::ssl::context> _sslContext;
    std::shared_ptr<dev::channel::ChannelServer> _server;

    std::map<int, dev::channel::ChannelSession::Ptr> _sessions;
    std::mutex _sessionMutex;

    std::map<std::string, dev::channel::ChannelSession::Ptr> _seq2session;
    std::mutex _seqMutex;

    // boost::atomic_int m_seq;
    std::atomic<size_t> m_seq;

    int _sessionCount = 1;

    std::shared_ptr<dev::p2p::P2PInterface> m_service;

    std::function<void(std::function<void(const std::string& receiptContext)>*)> m_callbackSetter;
    std::vector<dev::eth::Handler<int64_t> > m_handlers;
};

}  // namespace dev
