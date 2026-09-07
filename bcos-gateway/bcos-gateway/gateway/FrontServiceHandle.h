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
 * @brief statically-dispatched handle over the two concrete front implementations the gateway
 *        routes to (local in-process front service or remote tars client)
 * @file FrontServiceHandle.h
 */
#pragma once
#include <bcos-front/FrontService.h>
#ifndef ONLY_CPP_SDK
#include <bcos-tars-protocol/client/FrontServiceClient.h>
#endif
#include <utility>
#ifndef ONLY_CPP_SDK
#include <variant>
#endif

namespace bcos::gateway
{
// The gateway's local router table holds the in-process front::FrontService for local nodes and,
// in pro/max mode, a bcostars::FrontServiceClient for remote node-service processes (the tars
// client sources are excluded from ONLY_CPP_SDK builds, where only the in-process front exists).
// Both expose identical coroutine onReceive* signatures, so dispatch is static via std::visit.
#ifndef ONLY_CPP_SDK
using FrontServiceHandle =
    std::variant<front::FrontService::Ptr, bcostars::FrontServiceClient::Ptr>;
#else
using FrontServiceHandle = front::FrontService::Ptr;
#endif

template <typename Func>
decltype(auto) visitFrontService(FrontServiceHandle const& _handle, Func&& _func)
{
#ifndef ONLY_CPP_SDK
    return std::visit(std::forward<Func>(_func), _handle);
#else
    return std::forward<Func>(_func)(_handle);
#endif
}

inline task::Task<Error::Ptr> onReceiveMessage(FrontServiceHandle const& _handle,
    std::string _groupID, bcos::crypto::NodeIDPtr _nodeID, bytesConstRef _data)
{
    co_return co_await visitFrontService(_handle, [&](auto const& _frontService) {
        return _frontService->onReceiveMessage(std::move(_groupID), std::move(_nodeID), _data);
    });
}

inline task::Task<Error::Ptr> onReceiveGroupNodeInfo(FrontServiceHandle const& _handle,
    std::string _groupID, bcos::gateway::GroupNodeInfo::Ptr _groupNodeInfo)
{
    co_return co_await visitFrontService(_handle, [&](auto const& _frontService) {
        return _frontService->onReceiveGroupNodeInfo(
            std::move(_groupID), std::move(_groupNodeInfo));
    });
}

inline task::Task<Error::Ptr> onReceiveBroadcastMessage(FrontServiceHandle const& _handle,
    std::string _groupID, bcos::crypto::NodeIDPtr _nodeID, bytesConstRef _data)
{
    co_return co_await visitFrontService(_handle, [&](auto const& _frontService) {
        return _frontService->onReceiveBroadcastMessage(
            std::move(_groupID), std::move(_nodeID), _data);
    });
}
}  // namespace bcos::gateway
