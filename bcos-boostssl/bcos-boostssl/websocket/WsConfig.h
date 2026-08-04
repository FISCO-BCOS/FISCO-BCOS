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
 * @file WsConfig.h
 * @author: octopus
 * @date 2021-08-23
 */
#pragma once

#include <bcos-boostssl/context/ContextConfig.h>
#include <bcos-boostssl/httpserver/Common.h>
#include <bcos-boostssl/interfaces/NodeInfoDef.h>
#include <bcos-utilities/BoostLog.h>
#include <boost/asio/ip/tcp.hpp>
#include <cstdint>
#include <set>
#include <utility>

#define MIN_HEART_BEAT_PERIOD_MS (10000)
#define MIN_RECONNECT_PERIOD_MS (10000)
#define DEFAULT_MESSAGE_TIMEOUT_MS (-1)
#define DEFAULT_MAX_MESSAGE_SIZE (32 * 1024 * 1024)
#define MIN_THREAD_POOL_SIZE (1)

namespace bcos::boostssl::ws
{
using EndPoints = std::set<NodeIPEndpoint>;
using EndPointsPtr = std::shared_ptr<std::set<NodeIPEndpoint>>;
using EndPointsConstPtr = std::shared_ptr<const std::set<NodeIPEndpoint>>;

enum WsModel : uint16_t
{
    None = 0,
    Client = 0x01,
    Server = 0x10,
    Mixed = Client | Server
};

class WsConfig
{
public:
    using Ptr = std::shared_ptr<WsConfig>;
    using ConstPtr = std::shared_ptr<const WsConfig>;

private:
    // ws work model, as server or as client or server & client
    WsModel m_model = WsModel::None;

    // the listen ip when ws work as server
    std::string m_listenIP;
    // the listen port when ws work as server
    uint16_t m_listenPort;

    // whether smSSL or not, default not
    bool m_smSSL = false;

    // list of connected server nodes when ws work as client
    EndPointsPtr m_connectPeers;

    // thread pool size
    uint32_t m_threadPoolSize{4};

    // time out for send message
    int32_t m_sendMsgTimeout{DEFAULT_MESSAGE_TIMEOUT_MS};

    // time interval for reconnection
    uint32_t m_reconnectPeriod{MIN_RECONNECT_PERIOD_MS};

    // time interval for heartbeat
    uint32_t m_heartbeatPeriod{MIN_HEART_BEAT_PERIOD_MS};

    bool m_disableSsl{false};

    // cert config for boostssl
    std::shared_ptr<context::ContextConfig> m_contextConfig;

    // the max message to be send or read
    uint32_t m_maxMsgSize{DEFAULT_MAX_MESSAGE_SIZE};

    // cors config
    http::CorsConfig m_corsConfig;

public:
    void setModel(WsModel _model);
    WsModel model() const;

    bool asClient() const;
    bool asServer() const;

    void setListenIP(std::string _listenIP);
    std::string listenIP() const;

    void setListenPort(uint16_t _listenPort);
    uint16_t listenPort() const;

    void setSmSSL(bool _isSmSSL);
    bool smSSL() const;

    void setMaxMsgSize(uint32_t _maxMsgSize);
    uint32_t maxMsgSize() const;

    uint32_t reconnectPeriod() const;
    void setReconnectPeriod(uint32_t _reconnectPeriod);

    uint32_t heartbeatPeriod() const;
    void setHeartbeatPeriod(uint32_t _heartbeatPeriod);

    int32_t sendMsgTimeout() const;
    void setSendMsgTimeout(int32_t _sendMsgTimeout);

    uint32_t threadPoolSize() const;
    void setThreadPoolSize(uint32_t _threadPoolSize);

    EndPointsPtr connectPeers() const;
    void setConnectPeers(EndPointsPtr _connectPeers);
    bool disableSsl() const;
    void setDisableSsl(bool _disableSsl);

    std::shared_ptr<context::ContextConfig> contextConfig() const;
    void setContextConfig(std::shared_ptr<context::ContextConfig> _contextConfig);

    http::CorsConfig corsConfig() const;
    void setCorsConfig(http::CorsConfig _corsConfig);
};
}  // namespace bcos::boostssl::ws
