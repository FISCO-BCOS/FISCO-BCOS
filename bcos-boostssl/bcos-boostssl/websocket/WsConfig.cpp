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
 */

#include <bcos-boostssl/websocket/WsConfig.h>

using namespace bcos::boostssl::ws;

void WsConfig::setModel(WsModel _model)
{
    m_model = _model;
}

WsModel WsConfig::model() const
{
    return m_model;
}

bool WsConfig::asClient() const
{
    return (m_model & WsModel::Client) != 0;
}

bool WsConfig::asServer() const
{
    return (m_model & WsModel::Server) != 0;
}

void WsConfig::setListenIP(std::string _listenIP)
{
    m_listenIP = std::move(_listenIP);
}

std::string WsConfig::listenIP() const
{
    return m_listenIP;
}

void WsConfig::setListenPort(uint16_t _listenPort)
{
    m_listenPort = _listenPort;
}

uint16_t WsConfig::listenPort() const
{
    return m_listenPort;
}

void WsConfig::setSmSSL(bool _isSmSSL)
{
    m_smSSL = _isSmSSL;
}

bool WsConfig::smSSL() const
{
    return m_smSSL;
}

void WsConfig::setMaxMsgSize(uint32_t _maxMsgSize)
{
    m_maxMsgSize = _maxMsgSize;
}

uint32_t WsConfig::maxMsgSize() const
{
    return m_maxMsgSize;
}

uint32_t WsConfig::reconnectPeriod() const
{
    return m_reconnectPeriod > MIN_RECONNECT_PERIOD_MS ? m_reconnectPeriod :
                                                         MIN_RECONNECT_PERIOD_MS;
}

void WsConfig::setReconnectPeriod(uint32_t _reconnectPeriod)
{
    m_reconnectPeriod = _reconnectPeriod;
}

uint32_t WsConfig::heartbeatPeriod() const
{
    return m_heartbeatPeriod > MIN_HEART_BEAT_PERIOD_MS ? m_heartbeatPeriod :
                                                          MIN_HEART_BEAT_PERIOD_MS;
}

void WsConfig::setHeartbeatPeriod(uint32_t _heartbeatPeriod)
{
    m_heartbeatPeriod = _heartbeatPeriod;
}

int32_t WsConfig::sendMsgTimeout() const
{
    return m_sendMsgTimeout;
}

void WsConfig::setSendMsgTimeout(int32_t _sendMsgTimeout)
{
    m_sendMsgTimeout = _sendMsgTimeout;
}

uint32_t WsConfig::threadPoolSize() const
{
    return m_threadPoolSize != 0U ? m_threadPoolSize : MIN_THREAD_POOL_SIZE;
}

void WsConfig::setThreadPoolSize(uint32_t _threadPoolSize)
{
    m_threadPoolSize = _threadPoolSize;
}

EndPointsPtr WsConfig::connectPeers() const
{
    return m_connectPeers;
}

void WsConfig::setConnectPeers(EndPointsPtr _connectPeers)
{
    m_connectPeers = std::move(_connectPeers);
}

bool WsConfig::disableSsl() const
{
    return m_disableSsl;
}

void WsConfig::setDisableSsl(bool _disableSsl)
{
    m_disableSsl = _disableSsl;
}

std::shared_ptr<bcos::boostssl::context::ContextConfig> WsConfig::contextConfig() const
{
    return m_contextConfig;
}

void WsConfig::setContextConfig(std::shared_ptr<bcos::boostssl::context::ContextConfig> _contextConfig)
{
    m_contextConfig = std::move(_contextConfig);
}

bcos::boostssl::http::CorsConfig WsConfig::corsConfig() const
{
    return m_corsConfig;
}

void WsConfig::setCorsConfig(bcos::boostssl::http::CorsConfig _corsConfig)
{
    m_corsConfig = std::move(_corsConfig);
}