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
 * @file Sdk.h
 * @author: octopus
 * @date 2021-12-04
 */
#pragma once
#include "rpc/JsonRpcServiceImpl.h"
#include "utilities/logger/LogInitializer.h"
#include <bcos-boostssl/websocket/WsConfig.h>
#include <bcos-boostssl/websocket/WsService.h>
#include <bcos-cpp-sdk/amop/AMOP.h>
#include <bcos-cpp-sdk/event/EventSub.h>
#include <bcos-cpp-sdk/rpc/JsonRpcImpl.h>
#include <bcos-cpp-sdk/ws/Service.h>
#include <bcos-utilities/BoostLogInitializer.h>
#include <bcos-utilities/ThreadPool.h>
#include <memory>
#include <utility>

namespace bcos::cppsdk
{
class Sdk
{
public:
    using Ptr = std::shared_ptr<Sdk>;
    using UniquePtr = std::unique_ptr<Sdk>;

public:
    Sdk(bcos::cppsdk::service::Service::Ptr _service,
        bcos::cppsdk::jsonrpc::JsonRpcImpl::Ptr _jsonRpc, bcos::cppsdk::amop::AMOP::Ptr _amop,
        bcos::cppsdk::event::EventSub::Ptr _eventSub,
        bcos::cppsdk::jsonrpc::JsonRpcServiceImpl::Ptr _jsonRpcService)
      : m_service(std::move(_service)),
        m_jsonRpc(std::move(_jsonRpc)),
        m_amop(std::move(_amop)),
        m_eventSub(std::move(_eventSub)),
        m_jsonRpcService(std::move(_jsonRpcService))
    {}
    Sdk(bcos::cppsdk::service::Service::Ptr _service,
        bcos::cppsdk::jsonrpc::JsonRpcImpl::Ptr _jsonRpc, bcos::cppsdk::amop::AMOP::Ptr _amop,
        bcos::cppsdk::event::EventSub::Ptr _eventSub,
        bcos::cppsdk::jsonrpc::JsonRpcServiceImpl::Ptr _jsonRpcService, int strictConnectVersion)
      : Sdk(std::move(_service), std::move(_jsonRpc), std::move(_amop), std::move(_eventSub),
            std::move(_jsonRpcService))
    {
        this->m_service->setStrictConnectVersion(strictConnectVersion);
    }
    virtual ~Sdk() { stop(); }

private:
    bcos::cppsdk::service::Service::Ptr m_service;
    bcos::cppsdk::jsonrpc::JsonRpcImpl::Ptr m_jsonRpc;
    bcos::cppsdk::amop::AMOP::Ptr m_amop;
    bcos::cppsdk::event::EventSub::Ptr m_eventSub;
    bcos::cppsdk::jsonrpc::JsonRpcServiceImpl::Ptr m_jsonRpcService;

public:
    virtual void start()
    {
        if (m_service)
        {
            m_service->start();
        }

        if (m_jsonRpc)
        {
            m_jsonRpc->start();
        }

        if (m_amop)
        {
            m_amop->start();
        }

        if (m_eventSub)
        {
            m_eventSub->start();
        }
    }

    virtual void stop()
    {
        if (m_service)
        {
            m_service->stop();
        }

        if (m_jsonRpc)
        {
            m_jsonRpc->stop();
        }

        if (m_amop)
        {
            m_amop->stop();
        }

        if (m_eventSub)
        {
            m_eventSub->stop();
        }
    }

public:
    [[nodiscard]] bcos::cppsdk::service::Service::Ptr service() const { return m_service; }
    [[nodiscard]] bcos::cppsdk::jsonrpc::JsonRpcImpl::Ptr jsonRpc() const { return m_jsonRpc; }
    [[nodiscard]] bcos::cppsdk::jsonrpc::JsonRpcServiceImpl::Ptr jsonRpcService() const
    {
        return m_jsonRpcService;
    }
    [[nodiscard]] bcos::cppsdk::amop::AMOP::Ptr amop() const { return m_amop; }

    [[nodiscard]] bcos::cppsdk::event::EventSub::Ptr eventSub() const { return m_eventSub; }
    uint32_t localProtocolInfo() const { return m_service->localProtocolInfo(); }
    uint32_t negotiatedProtocolInfo() { return m_service->negotiatedProtocolInfo(); }
};

}  // namespace bcos::cppsdk
