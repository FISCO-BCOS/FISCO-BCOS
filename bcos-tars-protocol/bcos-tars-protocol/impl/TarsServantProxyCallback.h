/**
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
 * @brief tars implementation for tars ServantProxyCallback
 * @file TarsServantProxyCallback.h
 * @author: octopuswang
 * @date 2022-07-24
 */
#pragma once

#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/Error.h"
#include "bcos-utilities/Timer.h"
#include "servant/ServantProxy.h"
#include <boost/exception/detail/type_info.hpp>
#include <exception>
#include <functional>
#include <set>
#include <shared_mutex>

#define TARS_PING_PERIOD (1000)

namespace bcostars
{
struct TC_EndpointCompare
{
    bool operator()(const tars::TC_Endpoint& lhs, const tars::TC_Endpoint& rhs) const
    {
        return lhs.toString() < rhs.toString();
    }
};

using EndpointSet = std::set<tars::TC_Endpoint, TC_EndpointCompare>;

using TarsServantProxyOnConnectHandler = std::function<void(const tars::TC_Endpoint& ep)>;
using TarsServantProxyOnCloseHandler = std::function<void(const tars::TC_Endpoint& ep)>;

class TarsServantProxyCallback : public tars::ServantProxyCallback
{
public:
    using Ptr = std::shared_ptr<TarsServantProxyCallback>;
    using ConstPtr = std::shared_ptr<const TarsServantProxyCallback>;
    using UniquePtr = std::unique_ptr<const TarsServantProxyCallback>;

public:
    TarsServantProxyCallback(const std::string& _serviceName, tars::ServantProxy& _servantProxy)
      : m_serviceName(_serviceName), m_servantProxy(_servantProxy)
    {
        m_timer = std::make_shared<bcos::Timer>(TARS_PING_PERIOD, "tars_ping");
        m_timer->registerTimeoutHandler([this, _serviceName]() {
            ping();
            report();
            startTimer();
        });

        BCOS_LOG(INFO) << LOG_BADGE("[NEWOBJ][TarsServantProxyCallback]")
                       << LOG_KV("_serviceName", _serviceName) << LOG_KV("this", this);
    }

    TarsServantProxyCallback(TarsServantProxyCallback&&) = delete;
    TarsServantProxyCallback(const TarsServantProxyCallback&) = delete;
    const TarsServantProxyCallback& operator=(const TarsServantProxyCallback&) = delete;
    TarsServantProxyCallback& operator=(TarsServantProxyCallback&&) = delete;

    ~TarsServantProxyCallback() override
    {
        if (m_timer)
        {
            m_timer->stop();
        }

        BCOS_LOG(INFO) << LOG_BADGE("[DELOBJ][TarsServantProxyCallback]")
                       << LOG_KV("_serviceName", m_serviceName) << LOG_KV("this", this);
    }

public:
    int onDispatch(tars::ReqMessagePtr) override { return 0; }

    void onClose(const tars::TC_Endpoint& ep) override
    {
        auto p = addInactiveEndPoint(ep);
        BCOS_LOG(INFO) << LOG_BADGE("ServantProxyCallback::onClose")
                       << LOG_KV("endPoint", ep.toString()) << LOG_KV("result", p.first)
                       << LOG_KV("inactiveEndPoints size", p.second);

        if (p.first && m_onCloseHandler)
        {
            m_onCloseHandler(ep);
        }
    }

    void onConnect(const tars::TC_Endpoint& ep) override
    {
        auto p = addActiveEndPoint(ep);
        BCOS_LOG(INFO) << LOG_BADGE("ServantProxyCallback::onConnect")
                       << LOG_KV("endPoint", ep.toString()) << LOG_KV("result", p.first)
                       << LOG_KV("activeEndPoints size", p.second);

        if (p.first && m_onConnectHandler)
        {
            m_onConnectHandler(ep);
        }
    }

public:
    void startTimer()
    {
        if (m_timer)
        {
            m_timer->start();
        }
    }

    const std::string& serviceName() const { return m_serviceName; }

    auto activeEndPoints()
    {
        std::shared_lock l(x_endPoints);
        return m_activeEndPoints;
    }

    EndpointSet inactiveEndPoints()
    {
        std::shared_lock l(x_endPoints);
        return m_inactiveEndPoints;
    }

    std::pair<bool, std::size_t> addActiveEndPoint(const tars::TC_Endpoint& ep)
    {
        std::unique_lock l(x_endPoints);
        auto result = m_activeEndPoints.insert(ep);
        m_inactiveEndPoints.erase(ep);
        return std::make_pair(result.second, m_activeEndPoints.size());
    }

    std::pair<bool, std::size_t> addInactiveEndPoint(const tars::TC_Endpoint& ep)
    {
        {
            std::shared_lock l(x_endPoints);
            if (m_inactiveEndPoints.find(ep) != m_inactiveEndPoints.end())
            {
                return std::make_pair(false, m_inactiveEndPoints.size());
            }
        }

        {
            std::unique_lock l(x_endPoints);
            auto result = m_inactiveEndPoints.insert(ep);
            m_activeEndPoints.erase(ep);

            return std::make_pair(result.second, m_inactiveEndPoints.size());
        }
    }

    TarsServantProxyOnConnectHandler onConnectHandler() const { return m_onConnectHandler; }
    TarsServantProxyOnCloseHandler onCloseHandler() const { return m_onCloseHandler; }

    void setOnConnectHandler(TarsServantProxyOnConnectHandler _handler)
    {
        m_onConnectHandler = _handler;
    }

    void setOnCloseHandler(TarsServantProxyOnCloseHandler _handler) { m_onCloseHandler = _handler; }

    bool available() { return !activeEndPoints().empty(); }

    void ping()
    {
        try
        {
            // m_servantProxy.tars_ping();
            m_servantProxy.tars_async_ping();
            BCOS_LOG(TRACE) << LOG_BADGE("ServantProxyCallback::tars_async_ping")
                            << LOG_KV("serviceName", m_serviceName);
        }
        catch (const std::exception& _e)
        {
            BCOS_LOG(ERROR) << LOG_BADGE("ServantProxyCallback::tars_async_ping")
                            << LOG_KV("serviceName", m_serviceName)
                            << LOG_KV("e", boost::diagnostic_information(_e));
        }

        if (m_timer)
        {
            m_timer->restart();
        }
    }

    void report()
    {
        BCOS_LOG(INFO) << LOG_BADGE("ServantProxyCallback") << LOG_DESC("report endpoint")
                       << LOG_KV("serviceName", m_serviceName)
                       << LOG_KV("activeEndpoints size", activeEndPoints().size())
                       << LOG_KV("inactiveEndpoints size", inactiveEndPoints().size());
    }

private:
    std::string m_serviceName;

    // tars ServantProxy which the TarsServantProxyCallback belongs to
    tars::ServantProxy& m_servantProxy;

    // timer for ping and report
    std::shared_ptr<bcos::Timer> m_timer;

    // lock for m_activeEndPoints and m_inactiveEndPoints
    mutable std::shared_mutex x_endPoints;
    // all active tars endpoints
    EndpointSet m_activeEndPoints;
    // all inactive tars endpoints
    EndpointSet m_inactiveEndPoints;

    std::function<void(const tars::TC_Endpoint& ep)> m_onConnectHandler;
    std::function<void(const tars::TC_Endpoint& ep)> m_onCloseHandler;
};

}  // namespace bcostars