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
#include <bcos-boostssl/utilities/BoostLog.h>
#include <boost/asio/ip/tcp.hpp>
#include <cstdint>
#include <memory>
#include <vector>

#include <bcos-boostssl/network/Common.h>

#define MIN_HEART_BEAT_PERIOD_MS (10000)
#define MIN_RECONNECT_PERIOD_MS (10000)
#define DEFAULT_MESSAGE_TIMEOUT_MS (-1)
#define MIN_THREAD_POOL_SIZE (1)

namespace bcos
{
namespace boostssl
{
namespace ws
{
struct EndPoint
{
    std::string host;
    uint16_t port;
};

using EndPoints = std::vector<EndPoint>;
using EndPointsPtr = std::shared_ptr<std::vector<EndPoint>>;
using EndPointsConstPtr = std::shared_ptr<const std::vector<EndPoint>>;

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

    // list of connected server nodes when ws work as client
    EndPointsConstPtr m_connectedPeers;

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

public:
    void setModel(WsModel _model) { m_model = _model; }
    WsModel model() const { return m_model; }

    bool asClient() { return m_model & WsModel::Client; }
    bool asServer() const { return m_model & WsModel::Server; }

    void setListenIP(const std::string _listenIP) { m_listenIP = _listenIP; }
    std::string listenIP() const { return m_listenIP; }

    void setListenPort(uint16_t _listenPort) { m_listenPort = _listenPort; }
    uint16_t listenPort() const { return m_listenPort; }

    uint32_t reconnectPeriod() const
    {
        return m_reconnectPeriod > MIN_RECONNECT_PERIOD_MS ? m_reconnectPeriod :
                                                             MIN_RECONNECT_PERIOD_MS;
    }
    void setReconnectPeriod(uint32_t _reconnectPeriod) { m_reconnectPeriod = _reconnectPeriod; }

    uint32_t heartbeatPeriod() const
    {
        return m_heartbeatPeriod > MIN_HEART_BEAT_PERIOD_MS ? m_heartbeatPeriod :
                                                              MIN_HEART_BEAT_PERIOD_MS;
    }
    void setHeartbeatPeriod(uint32_t _heartbeatPeriod) { m_heartbeatPeriod = _heartbeatPeriod; }

    int32_t sendMsgTimeout() const { return m_sendMsgTimeout; }
    void setSendMsgTimeout(int32_t _sendMsgTimeout) { m_sendMsgTimeout = _sendMsgTimeout; }

    uint32_t threadPoolSize() const
    {
        return m_threadPoolSize ? m_threadPoolSize : MIN_THREAD_POOL_SIZE;
    }
    void setThreadPoolSize(uint32_t _threadPoolSize) { m_threadPoolSize = _threadPoolSize; }

    EndPointsConstPtr connectedPeers() { return m_connectedPeers; }
    void setConnectedPeers(EndPointsConstPtr _connectedPeers)
    {
        m_connectedPeers = _connectedPeers;
    }

    bool disableSsl() const { return m_disableSsl; }
    void setDisableSsl(bool _disableSsl) { m_disableSsl = _disableSsl; }

    std::shared_ptr<context::ContextConfig> contextConfig() const { return m_contextConfig; }
    void setContextConfig(std::shared_ptr<context::ContextConfig> _contextConfig)
    {
        m_contextConfig = _contextConfig;
    }
};
}  // namespace ws
}  // namespace boostssl
}  // namespace bcos
