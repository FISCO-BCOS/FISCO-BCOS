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
#include <memory>
#include <set>
#include <shared_mutex>

#define TARS_PING_PERIOD (3000)

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
        auto p = addInactiveEndpoint(ep);
        if (p.first && m_onCloseHandler)
        {
            m_onCloseHandler(ep);
        }
    }

    void onConnect(const tars::TC_Endpoint& ep) override
    {
        auto p = addActiveEndpoint(ep);
        BCOS_LOG(INFO) << LOG_BADGE("ServantProxyCallback::onConnect") << LOG_KV("this", this)
                       << LOG_KV("endpoint", ep.toString()) << LOG_KV("result", p.first)
                       << LOG_KV("activeEndpoints size", p.second);

        if (p.first && m_onConnectHandler)
        {
            m_onConnectHandler(ep);
        }
    }

public:
    void startTimer()
    {
        if (!m_timer)
        {
            return;
        }

        m_timer->registerTimeoutHandler([this]() {
            ping();
            report();
        });

        m_timer->start();
    }

    const std::string& serviceName() const { return m_serviceName; }

    auto activeEndpoints()
    {
        std::shared_lock l(x_endpoints);
        return m_activeEndpoints;
    }

    EndpointSet inactiveEndpoints()
    {
        std::shared_lock l(x_endpoints);
        return m_inactiveEndpoints;
    }

    std::pair<bool, std::size_t> addActiveEndpoint(const tars::TC_Endpoint& ep)
    {
        std::unique_lock l(x_endpoints);
        auto result = m_activeEndpoints.insert(ep);
        m_inactiveEndpoints.erase(ep);
        return std::make_pair(result.second, m_activeEndpoints.size());
    }

    std::pair<bool, std::size_t> addInactiveEndpoint(const tars::TC_Endpoint& ep)
    {
        {
            std::shared_lock l(x_endpoints);
            if (m_inactiveEndpoints.find(ep) != m_inactiveEndpoints.end())
            {
                return std::make_pair(false, m_inactiveEndpoints.size());
            }
        }

        {
            std::unique_lock l(x_endpoints);
            auto result = m_inactiveEndpoints.insert(ep);
            m_activeEndpoints.erase(ep);

            BCOS_LOG(INFO) << LOG_BADGE("ServantProxyCallback::addInactiveEndpoint")
                           << LOG_KV("this", this) << LOG_KV("result", result.second)
                           << LOG_KV("endpoint", ep.toString());

            return std::make_pair(result.second, m_inactiveEndpoints.size());
        }
    }

    TarsServantProxyOnConnectHandler onConnectHandler() const { return m_onConnectHandler; }
    TarsServantProxyOnCloseHandler onCloseHandler() const { return m_onCloseHandler; }

    void setOnConnectHandler(TarsServantProxyOnConnectHandler _handler)
    {
        m_onConnectHandler = _handler;
    }

    void setOnCloseHandler(TarsServantProxyOnCloseHandler _handler) { m_onCloseHandler = _handler; }

    bool available() { return !activeEndpoints().empty(); }

    void ping()
    {
        try
        {
            m_servantProxy.tars_async_ping();
        }
        catch (const std::exception& _e)
        {
            BCOS_LOG(ERROR) << LOG_BADGE("ServantProxyCallback::tars_async_ping")
                            << LOG_KV("serviceName", m_serviceName)
                            << LOG_KV("e", boost::diagnostic_information(_e));
        }
    }

    void report()
    {
        // BCOS_LOG(TRACE) << LOG_BADGE("ServantProxyCallback") << LOG_DESC("report endpoint")
        //                 << LOG_KV("serviceName", m_serviceName)
        //                 << LOG_KV("activeEndpoints size", activeEndpoints().size())
        //                 << LOG_KV("inactiveEndpoints size", inactiveEndpoints().size());

        if (m_timer)
        {
            m_timer->restart();
        }
    }

private:
    std::string m_serviceName;

    // tars ServantProxy which the TarsServantProxyCallback belongs to
    tars::ServantProxy& m_servantProxy;

    // timer for ping and report
    std::shared_ptr<bcos::Timer> m_timer;

    // lock for m_activeEndpoints and m_inactiveEndpoints
    mutable std::shared_mutex x_endpoints;
    // all active tars endpoints
    EndpointSet m_activeEndpoints;
    // all inactive tars endpoints
    EndpointSet m_inactiveEndpoints;

    std::function<void(const tars::TC_Endpoint& ep)> m_onConnectHandler;
    std::function<void(const tars::TC_Endpoint& ep)> m_onCloseHandler;
};

}  // namespace bcostars