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
 * @file Initializer.h
 * @author: octopus
 * @date 2021-08-21
 */
#pragma once
#include <bcos-boostssl/websocket/WsConfig.h>
#include <bcos-boostssl/websocket/WsService.h>
#include <bcos-cpp-sdk/Sdk.h>
#include <bcos-cpp-sdk/amop/AMOP.h>
#include <bcos-cpp-sdk/event/EventSub.h>
#include <bcos-cpp-sdk/rpc/JsonRpcImpl.h>
#include <bcos-cpp-sdk/ws/Service.h>
#include <bcos-utilities/ThreadPool.h>

namespace bcos
{
namespace cppsdk
{
class SdkFactory : public std::enable_shared_from_this<SdkFactory>
{
public:
    using Ptr = std::shared_ptr<SdkFactory>;

    SdkFactory();

public:
    bcos::cppsdk::service::Service::Ptr buildService(
        std::shared_ptr<bcos::boostssl::ws::WsConfig> _config);
    bcos::cppsdk::jsonrpc::JsonRpcImpl::Ptr buildJsonRpc(
        bcos::cppsdk::service::Service::Ptr _service);
    bcos::cppsdk::amop::AMOP::Ptr buildAMOP(bcos::cppsdk::service::Service::Ptr _service);
    bcos::cppsdk::event::EventSub::Ptr buildEventSub(bcos::cppsdk::service::Service::Ptr _service);

public:
    bcos::cppsdk::Sdk::UniquePtr buildSdk(
        std::shared_ptr<bcos::boostssl::ws::WsConfig> _config = nullptr);
    bcos::cppsdk::Sdk::UniquePtr buildSdk(const std::string& _configFile);

public:
    std::shared_ptr<bcos::boostssl::ws::WsConfig> config() const { return m_config; }
    void setConfig(std::shared_ptr<bcos::boostssl::ws::WsConfig> _config) { m_config = _config; }

private:
    std::shared_ptr<bcos::boostssl::ws::WsConfig> m_config;
};
}  // namespace cppsdk
}  // namespace bcos