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
#include "./rpc/JsonRpcServiceImpl.h"
#include <bcos-boostssl/websocket/WsConfig.h>
#include <bcos-boostssl/websocket/WsService.h>
#include <bcos-cpp-sdk/Sdk.h>
#include <bcos-cpp-sdk/amop/AMOP.h>
#include <bcos-cpp-sdk/event/EventSub.h>
#include <bcos-cpp-sdk/rpc/JsonRpcImpl.h>
#include <bcos-cpp-sdk/ws/Service.h>
#include <bcos-utilities/ThreadPool.h>
#include <utility>

namespace bcos::cppsdk
{
class SdkFactory
{
public:
    SdkFactory();
    ~SdkFactory() = default;
    SdkFactory(const SdkFactory&) = delete;
    SdkFactory& operator=(const SdkFactory&) = delete;
    SdkFactory(SdkFactory&&) = delete;
    SdkFactory& operator=(SdkFactory&&) = delete;

    bcos::cppsdk::service::Service::Ptr buildService(
        std::shared_ptr<bcos::boostssl::ws::WsConfig> _config);
    bcos::cppsdk::jsonrpc::JsonRpcImpl::Ptr buildJsonRpc(
        const bcos::cppsdk::service::Service::Ptr& _service,
        bool _sendRequestToHighestBlockNode = true);
    bcos::cppsdk::jsonrpc::JsonRpcServiceImpl::Ptr buildJsonRpcService(
        const bcos::cppsdk::jsonrpc::JsonRpcImpl::Ptr& _jsonRpc);
    bcos::cppsdk::amop::AMOP::Ptr buildAMOP(const bcos::cppsdk::service::Service::Ptr& _service);
    bcos::cppsdk::event::EventSub::Ptr buildEventSub(
        const bcos::cppsdk::service::Service::Ptr& _service);

    bcos::cppsdk::Sdk::UniquePtr buildSdk(
        std::shared_ptr<bcos::boostssl::ws::WsConfig> _config = nullptr,
        bool _sendRequestToHighestBlockNode = true);
    bcos::cppsdk::Sdk::UniquePtr buildSdk(const std::string& _configFile);
};
}  // namespace bcos::cppsdk
