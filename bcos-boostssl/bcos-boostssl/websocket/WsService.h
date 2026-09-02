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
#include <bcos-boostssl/websocket/Common.h>
#include <bcos-boostssl/websocket/WsConfig.h>
#include <bcos-boostssl/websocket/WsConnector.h>
#include <bcos-boostssl/websocket/WsMessage.h>
#include <bcos-boostssl/websocket/WsSession.h>
#include <bcos-boostssl/websocket/WsStream.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/IOServicePool.h>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/thread/thread.hpp>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace bcos::boostssl::ws
{
using WsSessions = std::vector<std::shared_ptr<WsSession>>;
using MsgHandler = std::function<void(WsMessage, std::shared_ptr<WsSession>)>;
using ConnectHandler = std::function<void(std::shared_ptr<WsSession>)>;
using DisconnectHandler = std::function<void(std::shared_ptr<WsSession>)>;

class WsService : public std::enable_shared_from_this<WsService>
{
public:
    using Ptr = std::shared_ptr<WsService>;
    explicit WsService();
    ~WsService();

    void start();
    void stop();
    void reconnect();
    void reportConnectedNodes();

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

    void onConnect(bcos::Error::Ptr _error, std::shared_ptr<WsSession> _session);
    void onDisconnect(bcos::Error::Ptr _error, std::shared_ptr<WsSession> _session);

    void onRecvMessage(WsMessage _msg, std::shared_ptr<WsSession> _session);

    void asyncSendMessage(const WsMessage& _msg, Options _options = Options(),
        RespCallBack _respFunc = RespCallBack());
    void asyncSendMessage(const WsSessions& _ss, const WsMessage& _msg,
        Options _options = Options(), RespCallBack _respFunc = RespCallBack());
    void asyncSendMessage(const std::set<std::string>& _endPoints, const WsMessage& _msg,
        Options _options = Options(), RespCallBack _respFunc = RespCallBack());

    void asyncSendMessageByEndPoint(const std::string& _endPoint, const WsMessage& _msg,
        Options _options = Options(), RespCallBack _respFunc = RespCallBack());

    void broadcastMessage(const WsMessage& _msg);
    void broadcastMessage(const WsSession::Ptrs& _ss, const WsMessage& _msg);

    // whether messages of this service use the raw wire format (fixed per service)
    bool rawMessage() const noexcept { return m_rawMessage; }
    void setRawMessage(bool _rawMessage) noexcept { m_rawMessage = _rawMessage; }

    int32_t waitConnectFinishTimeout() const;
    void setWaitConnectFinishTimeout(int32_t _timeout);

    void setIOServicePool(IOServicePool::Ptr _ioservicePool);

    std::shared_ptr<WsConnector> connector() const noexcept;
    void setConnector(std::shared_ptr<WsConnector> _connector);

    void setHostPort(std::string host, uint16_t port);
    std::string listenHost() const noexcept;
    uint16_t listenPort() const noexcept;

    WsConfig::Ptr config() const noexcept;
    void setConfig(WsConfig::Ptr _config);

    std::shared_ptr<bcos::boostssl::http::HttpServer> httpServer() const noexcept;
    void setHttpServer(std::shared_ptr<bcos::boostssl::http::HttpServer> _httpServer);
    void setTimerFactory(timer::TimerFactory::Ptr _timerFactory);
    timer::TimerFactory::Ptr timerFactory() const;

    bool registerMsgHandler(uint16_t _msgType, MsgHandler _msgHandler);

    void registerConnectHandler(ConnectHandler _connectHandler);

    void registerDisconnectHandler(DisconnectHandler _disconnectHandler);

    void setReconnectedPeers(EndPointsPtr _reconnectedPeers);
    EndPointsPtr reconnectedPeers() const;

private:
    bool m_running{false};

    int32_t m_waitConnectFinishTimeout = 30000;

    // raw wire format flag for all messages of this service
    bool m_rawMessage = false;
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

    // io service pool owns the io_context borrowed by m_timerFactory / timers.
    // Declared before them so it is destroyed after (reverse declaration order),
    // keeping the io_context alive as long as the timers may still use it.
    IOServicePool::Ptr m_ioservicePool;

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
    // type => handler, sparse map indexed by packet type: registered types are not dense
    // (e.g. WS_RAW_MESSAGE_TYPE(0xffff)), a flat array would waste memory on empty slots
    std::unordered_map<uint16_t, MsgHandler> m_msgType2Method;
    mutable SharedMutex x_msgTypeHandlers;
    // connected handlers, the handers will be called after ws protocol handshake
    // is complete
    std::vector<ConnectHandler> m_connectHandlers;
    // disconnected handlers, the handers will be called when ws session
    // disconnected
    std::vector<DisconnectHandler> m_disconnectHandlers;
};

}  // namespace bcos::boostssl::ws
