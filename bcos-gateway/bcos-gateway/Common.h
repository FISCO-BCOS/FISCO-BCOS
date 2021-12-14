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
 * @file Common.h
 * @author: octopus
 * @date 2021-05-04
 */
#pragma once
#include "libnetwork/Common.h"
#include <bcos-framework/interfaces/protocol/ServiceDesc.h>
#include <bcos-framework/libutilities/Exceptions.h>
#include <tarscpp/servant/Application.h>

#define GATEWAY_LOG(LEVEL) BCOS_LOG(LEVEL) << "[Gateway][Gateway]"
#define GATEWAY_CONFIG_LOG(LEVEL) BCOS_LOG(LEVEL) << "[Gateway][Config]"
#define GATEWAY_FACTORY_LOG(LEVEL) BCOS_LOG(LEVEL) << "[Gateway][Factory]"
#define NODE_MANAGER_LOG(LEVEL) BCOS_LOG(LEVEL) << "[Gateway][GatewayNodeManager]"

namespace bcos
{
namespace gateway
{
template <typename T, typename S, typename... Args>
std::pair<std::shared_ptr<T>, S> createServiceClient(
    std::string const& _serviceName, std::string const& _servantName, const Args&... _args)
{
    auto servantName = bcos::protocol::getPrxDesc(_serviceName, _servantName);
    auto prx = Application::getCommunicator()->stringToProxy<S>(servantName);
    return std::make_pair(std::make_shared<T>(prx, _args...), prx);
}
}  // namespace gateway
}  // namespace bcos