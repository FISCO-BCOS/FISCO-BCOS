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
#include <bcos-boostssl/utilities/Common.h>
#include <bcos-boostssl/utilities/ThreadPool.h>
#include <bcos-boostssl/websocket/Common.h>
#include <bcos-boostssl/websocket/WsConfig.h>
#include <bcos-boostssl/websocket/WsConnector.h>
#include <bcos-boostssl/websocket/WsMessage.h>
#include <bcos-boostssl/websocket/WsSession.h>
#include <bcos-boostssl/websocket/WsStream.h>
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
namespace utilities
{
class ThreadPool;
}

namespace ws
{
using WsSessions = std::vector<std::shared_ptr<WsSession>>;
using MsgHandler = std::function<void(std::shared_ptr<WsMessage>, std::shared_ptr<WsSession>)>;
using ConnectHandler = std::function<void(std::shared_ptr<WsSession>)>;
using DisconnectHandler = std::function<void(std::shared_ptr<WsSession>)>;
using HandshakeHandler = std::function<void(
    utilities::Error::Ptr _error, std::shared_ptr<WsMessage>, std::shared_ptr<WsSession>)>;

class WsService : public std::enable_shared_from_this<WsService>
{
public:
    using Ptr = std::shared_ptr<WsService>;
    WsService();
    virtual ~WsService();

public:
    virtual void start();
    virtual void stop();
    virtual void reconnect();
    virtual void heartbeat();

    void asyncConnectOnce(EndPointsConstPtr _peers);

public:
    void startIocThread();

public:
    std::shared_ptr<WsSession> newSession(std::shared_ptr<WsStream> _stream);
    std::shared_ptr<WsSession> getSession(const std::string& _endPoint);
    void addSession(std::shared_ptr<WsSession> _session);
    void removeSession(const std::string& _endPoint);
    WsSessions sessions();

public:
    virtual void onConnect(utilities::Error::Ptr _error, std::shared_ptr<WsSession> _session);
    virtual void onDisconnect(utilities::Error::Ptr _error, std::shared_ptr<WsSession> _session);

    virtual void onRecvMessage(
        std::shared_ptr<WsMessage> _msg, std::shared_ptr<WsSession> _session);

    virtual void asyncSendMessage(std::shared_ptr<WsMessage> _msg, Options _options = Options(),
        RespCallBack _respFunc = RespCallBack());
    virtual void asyncSendMessage(const WsSessions& _ss, std::shared_ptr<WsMessage> _msg,
        Options _options = Options(), RespCallBack _respFunc = RespCallBack());
    virtual void asyncSendMessage(const std::set<std::string>& _endPoints,
        std::shared_ptr<WsMessage> _msg, Options _options = Options(),
        RespCallBack _respFunc = RespCallBack());

    virtual void asyncSendMessageByEndPoint(const std::string& _endPoint,
        std::shared_ptr<WsMessage> _msg, Options _options = Options(),
        RespCallBack _respFunc = RespCallBack());

    virtual void broadcastMessage(std::shared_ptr<WsMessage> _msg);
    virtual void broadcastMessage(const WsSession::Ptrs& _ss, std::shared_ptr<WsMessage> _msg);

public:
    std::shared_ptr<WsStreamFactory> wsStreamFactory() { return m_wsStreamFactory; }
    void setWsStreamFactory(std::shared_ptr<WsStreamFactory> _wsStreamFactory)
    {
        m_wsStreamFactory = _wsStreamFactory;
    }

    std::shared_ptr<WsMessageFactory> messageFactory() { return m_messageFactory; }
    void setMessageFactory(std::shared_ptr<WsMessageFactory> _messageFactory)
    {
        m_messageFactory = _messageFactory;
    }

    std::shared_ptr<utilities::ThreadPool> threadPool() const { return m_threadPool; }
    void setThreadPool(std::shared_ptr<utilities::ThreadPool> _threadPool)
    {
        m_threadPool = _threadPool;
    }

    bool disableSsl() const { return m_disableSsl; }
    void setDisableSsl(bool _disableSsl) { m_disableSsl = _disableSsl; }

    bool waitConnectFinish() const { return m_waitConnectFinish; }
    void setWaitConnectFinish(bool _b) { m_waitConnectFinish = _b; }

    int32_t waitConnectFinishTimeout() const { return m_waitConnectFinishTimeout; }
    void setWaitConnectFinishTimeout(int32_t _timeout) { m_waitConnectFinishTimeout = _timeout; }

    std::shared_ptr<boost::asio::io_context> ioc() const { return m_ioc; }
    void setIoc(std::shared_ptr<boost::asio::io_context> _ioc) { m_ioc = _ioc; }

    std::shared_ptr<boost::asio::ssl::context> ctx() const { return m_ctx; }
    void setCtx(std::shared_ptr<boost::asio::ssl::context> _ctx) { m_ctx = _ctx; }

    std::shared_ptr<WsConnector> connector() const { return m_connector; }
    void setConnector(std::shared_ptr<WsConnector> _connector) { m_connector = _connector; }

    std::shared_ptr<WsConfig> config() const { return m_config; }
    void setConfig(std::shared_ptr<WsConfig> _config) { m_config = _config; }

    std::shared_ptr<bcos::boostssl::http::HttpServer> httpServer() const { return m_httpServer; }
    void setHttpServer(std::shared_ptr<bcos::boostssl::http::HttpServer> _httpServer)
    {
        m_httpServer = _httpServer;
    }

    bool registerMsgHandler(uint32_t _msgType, MsgHandler _msgHandler);

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

    int32_t sendMsgTimeout() const { return m_sendMsgTimeout; }
    void setSendMsgTimeout(int32_t _sendMsgTimeout) { m_sendMsgTimeout = _sendMsgTimeout; }

public:
    void waitForConnectionEstablish();

private:
    bool m_running{false};
    bool m_disableSsl{false};
    bool m_waitConnectFinish{false};
    //
    int32_t m_sendMsgTimeout = -1;
    // default timeout , 30s
    int32_t m_waitConnectFinishTimeout = 30000;

    // WsMessageFactory
    std::shared_ptr<WsMessageFactory> m_messageFactory;
    // WsStreamFactory
    std::shared_ptr<WsStreamFactory> m_wsStreamFactory;
    // ThreadPool
    std::shared_ptr<utilities::ThreadPool> m_threadPool;
    // Config
    std::shared_ptr<WsConfig> m_config;
    // ws connector
    std::shared_ptr<WsConnector> m_connector;
    // io context
    std::shared_ptr<boost::asio::io_context> m_ioc;
    // ssl context
    std::shared_ptr<boost::asio::ssl::context> m_ctx = nullptr;
    // thread for ioc
    std::shared_ptr<std::thread> m_iocThread;
    // reconnect timer
    std::shared_ptr<boost::asio::deadline_timer> m_reconnect;
    // heartbeat timer
    std::shared_ptr<boost::asio::deadline_timer> m_heartbeat;
    // http server
    std::shared_ptr<bcos::boostssl::http::HttpServer> m_httpServer;

private:
    // mutex for m_sessions
    mutable boost::shared_mutex x_mutex;
    // all active sessions
    std::unordered_map<std::string, std::shared_ptr<WsSession>> m_sessions;
    // type => handler
    std::unordered_map<uint32_t, MsgHandler> m_msgType2Method;
    // connected handlers, the handers will be called after ws protocol handshake is complete
    std::vector<ConnectHandler> m_connectHandlers;
    // disconnected handlers, the handers will be called when ws session disconnected
    std::vector<DisconnectHandler> m_disconnectHandlers;
    // disconnected handlers, the handers will be called when ws session disconnected
    std::vector<HandshakeHandler> m_handshakeHandlers;
};

}  // namespace ws
}  // namespace boostssl
}  // namespace bcos