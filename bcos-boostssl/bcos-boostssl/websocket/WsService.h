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

#include "bcos-utilities/NewTimer.h"
#include <bcos-boostssl/httpserver/HttpServer.h>
#include <bcos-boostssl/interfaces/MessageFace.h>
#include <bcos-boostssl/websocket/Common.h>
#include <bcos-boostssl/websocket/WsConfig.h>
#include <bcos-boostssl/websocket/WsConnector.h>
#include <bcos-boostssl/websocket/WsMessage.h>
#include <bcos-boostssl/websocket/WsSession.h>
#include <bcos-boostssl/websocket/WsStream.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/IOServicePool.h>
#include <bcos-utilities/ThreadPool.h>
#include <oneapi/tbb/task_arena.h>
#include <oneapi/tbb/task_group.h>
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

namespace bcos::boostssl::ws
{
using WsSessions = std::vector<std::shared_ptr<WsSession>>;
using MsgHandler =
    std::function<void(std::shared_ptr<boostssl::MessageFace>, std::shared_ptr<WsSession>)>;
using ConnectHandler = std::function<void(std::shared_ptr<WsSession>)>;
using DisconnectHandler = std::function<void(std::shared_ptr<WsSession>)>;
using HandshakeHandler = std::function<void(
    bcos::Error::Ptr, std::shared_ptr<boostssl::MessageFace>, std::shared_ptr<WsSession>)>;

class WsService : public std::enable_shared_from_this<WsService>
{
public:
    using Ptr = std::shared_ptr<WsService>;
    explicit WsService();
    virtual ~WsService();

    virtual void start();
    virtual void stop();
    virtual void reconnect();
    virtual void reportConnectedNodes();

    std::shared_ptr<std::vector<std::shared_ptr<
        std::promise<std::tuple<boost::beast::error_code, std::string, std::string>>>>>
    asyncConnectToEndpoints(EndPointsPtr _peers);

    inline static std::string genConnectError(
        const std::string& _error, const std::string& endpoint, bool end);
    void syncConnectToEndpoints(EndPointsPtr _peers);

    std::shared_ptr<WsSession> newSession(
        std::shared_ptr<WsStreamDelegate> _wsStreamDelegate, std::string const& _nodeId);
    std::shared_ptr<WsSession> getSession(const std::string& _endPoint);
    void addSession(std::shared_ptr<WsSession> _session);
    void removeSession(const std::string& _endPoint);
    WsSessions sessions();

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

    std::shared_ptr<MessageFaceFactory> messageFactory() { return m_messageFactory; }
    void setMessageFactory(std::shared_ptr<MessageFaceFactory> _messageFactory)
    {
        m_messageFactory = std::move(_messageFactory);
    }

    std::shared_ptr<WsSessionFactory> sessionFactory() { return m_sessionFactory; }
    void setSessionFactory(std::shared_ptr<WsSessionFactory> _sessionFactory)
    {
        m_sessionFactory = std::move(_sessionFactory);
    }
    int32_t waitConnectFinishTimeout() const { return m_waitConnectFinishTimeout; }
    void setWaitConnectFinishTimeout(int32_t _timeout) { m_waitConnectFinishTimeout = _timeout; }

    void setIOServicePool(IOServicePool::Ptr _ioservicePool)
    {
        m_ioservicePool = std::move(_ioservicePool);
        m_timerIoc = m_ioservicePool->getIOService();
    }

    std::shared_ptr<WsConnector> connector() const noexcept { return m_connector; }
    void setConnector(std::shared_ptr<WsConnector> _connector)
    {
        m_connector = std::move(_connector);
    }

    void setHostPort(std::string host, uint16_t port)
    {
        m_listenHost = std::move(host);
        m_listenPort = port;
    }
    std::string listenHost() const noexcept { return m_listenHost; }
    uint16_t listenPort() const noexcept { return m_listenPort; }

    WsConfig::Ptr config() const noexcept { return m_config; }
    void setConfig(WsConfig::Ptr _config) { m_config = std::move(_config); }

    std::shared_ptr<bcos::boostssl::http::HttpServer> httpServer() const noexcept
    {
        return m_httpServer;
    }
    void setHttpServer(std::shared_ptr<bcos::boostssl::http::HttpServer> _httpServer)
    {
        m_httpServer = std::move(_httpServer);
    }
    void setTimerFactory(timer::TimerFactory::Ptr _timerFactory)
    {
        m_timerFactory = std::move(_timerFactory);
    }
    timer::TimerFactory::Ptr timerFactory() const { return m_timerFactory; }

    bool registerMsgHandler(uint16_t _msgType, MsgHandler _msgHandler);

    MsgHandler getMsgHandler(uint16_t _type);

    bool eraseMsgHandler(uint16_t _msgType);

    void registerConnectHandler(ConnectHandler _connectHandler)
    {
        m_connectHandlers.push_back(std::move(_connectHandler));
    }

    void registerDisconnectHandler(DisconnectHandler _disconnectHandler)
    {
        m_disconnectHandlers.push_back(std::move(_disconnectHandler));
    }

    void registerHandshakeHandler(HandshakeHandler _handshakeHandler)
    {
        m_handshakeHandlers.push_back(std::move(_handshakeHandler));
    }

    void setReconnectedPeers(EndPointsPtr _reconnectedPeers)
    {
        WriteGuard l(x_peers);
        m_reconnectedPeers = std::move(_reconnectedPeers);
    }
    EndPointsPtr reconnectedPeers() const
    {
        ReadGuard l(x_peers);
        return m_reconnectedPeers;
    }

    // init
    void initTaskArena(uint32_t _taskArenaPoolSize)
    {
        m_taskArena.initialize(_taskArenaPoolSize, 0);
    }

private:
    bool m_running{false};

    tbb::task_arena m_taskArena;
    tbb::task_group m_taskGroup;

    int32_t m_waitConnectFinishTimeout = 30000;

    // MessageFaceFactory
    std::shared_ptr<MessageFaceFactory> m_messageFactory;
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
    std::shared_ptr<timer::Timer> m_statTimer;
    // reconnect timer
    std::shared_ptr<timer::Timer> m_reconnectTimer;
    // http server
    std::shared_ptr<bcos::boostssl::http::HttpServer> m_httpServer;
    // timer
    timer::TimerFactory::Ptr m_timerFactory = nullptr;

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

    IOServicePool::Ptr m_ioservicePool;

    std::shared_ptr<boost::asio::io_context> m_timerIoc;
};

}  // namespace bcos::boostssl::ws
