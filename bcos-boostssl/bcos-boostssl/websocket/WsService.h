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
 * @file WsService.h
 * @author: octopus
 * @date 2021-07-28
 */
#pragma once

#include <bcos-boostssl/httpserver/HttpServer.h>
#include <bcos-boostssl/interfaces/MessageFace.h>
#include <bcos-boostssl/websocket/Common.h>
#include <bcos-boostssl/websocket/WsConfig.h>
#include <bcos-boostssl/websocket/WsConnector.h>
#include <bcos-boostssl/websocket/WsMessage.h>
#include <bcos-boostssl/websocket/WsSession.h>
#include <bcos-boostssl/websocket/WsStream.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/ThreadPool.h>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/thread/thread.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bcos
{
namespace boostssl
{
namespace ws
{
using WsSessions = std::vector<std::shared_ptr<WsSession>>;
using MsgHandler =
    std::function<void(std::shared_ptr<boostssl::MessageFace>, std::shared_ptr<WsSession>)>;
using ConnectHandler = std::function<void(std::shared_ptr<WsSession>)>;
using DisconnectHandler = std::function<void(std::shared_ptr<WsSession>)>;
using HandshakeHandler = std::function<void(
    bcos::Error::Ptr _error, std::shared_ptr<boostssl::MessageFace>, std::shared_ptr<WsSession>)>;

class WsService : public std::enable_shared_from_this<WsService>
{
public:
    using Ptr = std::shared_ptr<WsService>;
    WsService(std::string _moduleName = "DEFAULT");
    virtual ~WsService();

public:
    virtual void start();
    virtual void stop();
    virtual void reconnect();
    virtual void reportConnectedNodes();

    std::shared_ptr<std::vector<std::shared_ptr<
        std::promise<std::tuple<boost::beast::error_code, std::string, std::string>>>>>
    asyncConnectToEndpoints(EndPointsPtr _peers);

    std::string genConnectError(const std::string& _error, const std::string& endpoint, bool end);
    void syncConnectToEndpoints(EndPointsPtr _peers);

public:
    void startIocThread();
    void stopIocThread();

public:
    std::shared_ptr<WsSession> newSession(
        std::shared_ptr<WsStreamDelegate> _wsStreamDelegate, std::string const& _nodeId);
    std::shared_ptr<WsSession> getSession(const std::string& _endPoint);
    void addSession(std::shared_ptr<WsSession> _session);
    void removeSession(const std::string& _endPoint);
    WsSessions sessions();

public:
    virtual void onConnect(bcos::Error::Ptr _error, std::shared_ptr<WsSession> _session);
    virtual void onDisconnect(bcos::Error::Ptr _error, std::shared_ptr<WsSession> _session);

    virtual void onRecvMessage(
        std::shared_ptr<boostssl::MessageFace> _msg, std::shared_ptr<WsSession> _session);

    virtual void asyncSendMessage(std::shared_ptr<boostssl::MessageFace> _msg,
        Options _options = Options(), RespCallBack _respFunc = RespCallBack());
    virtual void asyncSendMessage(const WsSessions& _ss,
        std::shared_ptr<boostssl::MessageFace> _msg, Options _options = Options(),
        RespCallBack _respFunc = RespCallBack());
    virtual void asyncSendMessage(const std::set<std::string>& _endPoints,
        std::shared_ptr<boostssl::MessageFace> _msg, Options _options = Options(),
        RespCallBack _respFunc = RespCallBack());

    virtual void asyncSendMessageByEndPoint(const std::string& _endPoint,
        std::shared_ptr<boostssl::MessageFace> _msg, Options _options = Options(),
        RespCallBack _respFunc = RespCallBack());

    virtual void broadcastMessage(std::shared_ptr<boostssl::MessageFace> _msg);
    virtual void broadcastMessage(
        const WsSession::Ptrs& _ss, std::shared_ptr<boostssl::MessageFace> _msg);

public:
    std::shared_ptr<MessageFaceFactory> messageFactory() { return m_messageFactory; }
    void setMessageFactory(std::shared_ptr<MessageFaceFactory> _messageFactory)
    {
        m_messageFactory = _messageFactory;
    }

    std::shared_ptr<WsSessionFactory> sessionFactory() { return m_sessionFactory; }
    void setSessionFactory(std::shared_ptr<WsSessionFactory> _sessionFactory)
    {
        m_sessionFactory = _sessionFactory;
    }

    std::size_t iocThreadCount() const { return m_iocThreadCount; }
    void setIocThreadCount(std::size_t _iocThreadCount) { m_iocThreadCount = _iocThreadCount; }

    int32_t waitConnectFinishTimeout() const { return m_waitConnectFinishTimeout; }
    void setWaitConnectFinishTimeout(int32_t _timeout) { m_waitConnectFinishTimeout = _timeout; }

    std::string moduleName() { return m_moduleName; }
    void setModuleName(std::string _moduleName) { m_moduleName = _moduleName; }

    std::shared_ptr<bcos::ThreadPool> threadPool() const { return m_threadPool; }
    void setThreadPool(std::shared_ptr<bcos::ThreadPool> _threadPool)
    {
        m_threadPool = _threadPool;
    }

    std::shared_ptr<boost::asio::io_context> ioc() const { return m_ioc; }
    void setIoc(std::shared_ptr<boost::asio::io_context> _ioc) { m_ioc = _ioc; }

    std::shared_ptr<boost::asio::ssl::context> ctx() const { return m_ctx; }
    void setCtx(std::shared_ptr<boost::asio::ssl::context> _ctx) { m_ctx = _ctx; }

    std::shared_ptr<WsConnector> connector() const { return m_connector; }
    void setConnector(std::shared_ptr<WsConnector> _connector) { m_connector = _connector; }

    void setHostPort(std::string host, uint16_t port)
    {
        m_listenHost = host;
        m_listenPort = port;
    }
    std::string listenHost() { return m_listenHost; }
    uint16_t listenPort() { return m_listenPort; }

    WsConfig::Ptr config() const { return m_config; }
    void setConfig(WsConfig::Ptr _config) { m_config = _config; }

    std::shared_ptr<bcos::boostssl::http::HttpServer> httpServer() const { return m_httpServer; }
    void setHttpServer(std::shared_ptr<bcos::boostssl::http::HttpServer> _httpServer)
    {
        m_httpServer = _httpServer;
    }

    bool registerMsgHandler(uint16_t _msgType, MsgHandler _msgHandler);

    MsgHandler getMsgHandler(uint16_t _type);

    bool eraseMsgHandler(uint16_t _msgType);

    void registerConnectHandler(ConnectHandler _connectHandler)
    {
        m_connectHandlers.push_back(_connectHandler);
    }

    void registerDisconnectHandler(DisconnectHandler _disconnectHandler)
    {
        m_disconnectHandlers.push_back(_disconnectHandler);
    }

    void registerHandshakeHandler(HandshakeHandler _handshakeHandler)
    {
        m_handshakeHandlers.push_back(_handshakeHandler);
    }

    void setReconnectedPeers(EndPointsPtr _reconnectedPeers)
    {
        WriteGuard l(x_peers);
        m_reconnectedPeers = _reconnectedPeers;
    }
    EndPointsPtr reconnectedPeers() const
    {
        ReadGuard l(x_peers);
        return m_reconnectedPeers;
    }

private:
    bool m_running{false};

    int32_t m_waitConnectFinishTimeout = 30000;
    std::string m_moduleName;

    // MessageFaceFactory
    std::shared_ptr<MessageFaceFactory> m_messageFactory;
    // ThreadPool
    std::shared_ptr<bcos::ThreadPool> m_threadPool;
    // listen host port
    std::string m_listenHost = "";
    uint16_t m_listenPort = 0;
    // nodeID
    std::string m_nodeID;
    // Config
    std::shared_ptr<WsConfig> m_config;

    // list of reconnected server nodes updated by upper module, such as p2pservice
    EndPointsPtr m_reconnectedPeers;
    mutable bcos::SharedMutex x_peers;

    // ws connector
    std::shared_ptr<WsConnector> m_connector;
    // io context
    std::shared_ptr<boost::asio::io_context> m_ioc;
    // ssl context
    std::shared_ptr<boost::asio::ssl::context> m_ctx = nullptr;
    // thread for ioc
    std::shared_ptr<std::vector<std::thread>> m_iocThreads;
    // reconnect timer
    std::shared_ptr<boost::asio::deadline_timer> m_reconnect;
    // heartbeat timer
    std::shared_ptr<boost::asio::deadline_timer> m_heartbeat;
    // http server
    std::shared_ptr<bcos::boostssl::http::HttpServer> m_httpServer;

private:
    std::size_t m_iocThreadCount;
    // mutex for m_sessions
    mutable boost::shared_mutex x_mutex;
    // all active sessions
    std::unordered_map<std::string, std::shared_ptr<WsSession>> m_sessions;
    // type => handler
    std::unordered_map<uint16_t, MsgHandler> m_msgType2Method;
    mutable SharedMutex x_msgTypeHandlers;
    // connected handlers, the handers will be called after ws protocol handshake
    // is complete
    std::vector<ConnectHandler> m_connectHandlers;
    // disconnected handlers, the handers will be called when ws session
    // disconnected
    std::vector<DisconnectHandler> m_disconnectHandlers;
    // handshake handlers, the handers will be called when ws session
    // disconnected
    std::vector<HandshakeHandler> m_handshakeHandlers;
    // sessionFactory
    WsSessionFactory::Ptr m_sessionFactory;
};

}  // namespace ws
}  // namespace boostssl
}  // namespace bcos