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
 * @brief statically-dispatched handle over the two concrete gateway implementations a consumer
 *        talks to (local in-process gateway or remote tars gateway-service client)
 * @file GatewayHandle.h
 */
#pragma once
#include <bcos-front/FrontServiceGateway.h>
#include <bcos-gateway/Gateway.h>
#ifndef ONLY_CPP_SDK
#include <bcos-tars-protocol/client/GatewayServiceClient.h>
#endif
#include <utility>
#ifndef ONLY_CPP_SDK
#include <variant>
#endif

namespace bcos::gateway
{
// Consumers (rpc, libinitializer, the air node, the legacy lightnode) talk either to the
// in-process gateway::Gateway (air mode) or, in pro/max mode, to a bcostars::GatewayServiceClient
// proxying the remote gateway-service process (the tars client sources are excluded from
// ONLY_CPP_SDK builds, where only the in-process gateway exists). Both expose identical
// signatures, so dispatch is static via std::visit.
//
// The dispatch functions below are NOT coroutines: they synchronously select the branch and
// return the callee's task as-is, so no extra coroutine frame is introduced and the reference
// parameters keep referring to the caller's objects (the caller must keep them alive until the
// returned task completes — the same contract the former interface documented).
#ifndef ONLY_CPP_SDK
using GatewayHandle = std::variant<Gateway::Ptr, bcostars::GatewayServiceClient::Ptr>;
#else
using GatewayHandle = Gateway::Ptr;
#endif

template <typename Func>
decltype(auto) visitGateway(GatewayHandle const& _handle, Func&& _func)
{
#ifndef ONLY_CPP_SDK
    return std::visit(std::forward<Func>(_func), _handle);
#else
    return std::forward<Func>(_func)(_handle);
#endif
}

inline void start(GatewayHandle const& _handle)
{
    visitGateway(_handle, [](auto const& _gateway) { _gateway->start(); });
}

inline void stop(GatewayHandle const& _handle)
{
    visitGateway(_handle, [](auto const& _gateway) { _gateway->stop(); });
}

inline task::Task<std::tuple<Error::Ptr, bcos::gateway::GroupNodeInfo::Ptr>> getGroupNodeInfo(
    GatewayHandle const& _handle, const std::string& _groupID)
{
    return visitGateway(
        _handle, [&](auto const& _gateway) { return _gateway->getGroupNodeInfo(_groupID); });
}

inline task::Task<std::tuple<Error::Ptr, GatewayInfo::Ptr, GatewayInfosPtr>> getPeers(
    GatewayHandle const& _handle)
{
    return visitGateway(_handle, [](auto const& _gateway) { return _gateway->getPeers(); });
}

inline task::Task<void> broadcastMessage(GatewayHandle const& _handle, uint16_t _type,
    std::string_view _groupID, int _moduleID, const bcos::crypto::NodeID& _srcNodeID,
    ::ranges::any_view<bytesConstRef, ::ranges::category::forward> _payloads)
{
    return visitGateway(_handle, [&](auto const& _gateway) {
        return _gateway->broadcastMessage(
            _type, _groupID, _moduleID, _srcNodeID, std::move(_payloads));
    });
}

// (coroutine, zero-copy) the payload views must be kept alive by the caller for the duration of
// the co_await.
inline task::Task<Error::Ptr> sendMessageByNodeID(GatewayHandle const& _handle,
    const std::string& _groupID, int _moduleID, bcos::crypto::NodeIDPtr _srcNodeID,
    bcos::crypto::NodeIDPtr _dstNodeID,
    ::ranges::any_view<bytesConstRef, ::ranges::category::forward> _payloads)
{
    return visitGateway(_handle, [&](auto const& _gateway) {
        return _gateway->sendMessageByNodeID(
            _groupID, _moduleID, std::move(_srcNodeID), std::move(_dstNodeID), std::move(_payloads));
    });
}

inline void asyncNotifyGroupInfo(GatewayHandle const& _handle,
    bcos::group::GroupInfo::Ptr _groupInfo, std::function<void(Error::Ptr&&)> _callback)
{
    visitGateway(_handle, [&](auto const& _gateway) {
        _gateway->asyncNotifyGroupInfo(std::move(_groupInfo), std::move(_callback));
    });
}

inline task::Task<std::tuple<Error::Ptr, int16_t, bcos::bytes>> sendMessageByTopic(
    GatewayHandle const& _handle, const std::string& _topic, bcos::bytesConstRef _data)
{
    return visitGateway(
        _handle, [&](auto const& _gateway) { return _gateway->sendMessageByTopic(_topic, _data); });
}

inline task::Task<void> sendBroadcastMessageByTopic(
    GatewayHandle const& _handle, const std::string& _topic, bcos::bytesConstRef _data)
{
    return visitGateway(_handle, [&](auto const& _gateway) {
        return _gateway->sendBroadcastMessageByTopic(_topic, _data);
    });
}

inline void asyncSubscribeTopic(GatewayHandle const& _handle, std::string const& _clientID,
    std::string const& _topicInfo, std::function<void(Error::Ptr&&)> _callback)
{
    visitGateway(_handle, [&](auto const& _gateway) {
        _gateway->asyncSubscribeTopic(_clientID, _topicInfo, std::move(_callback));
    });
}

inline void asyncRemoveTopic(GatewayHandle const& _handle, std::string const& _clientID,
    std::vector<std::string> const& _topicList, std::function<void(Error::Ptr&&)> _callback)
{
    visitGateway(_handle, [&](auto const& _gateway) {
        _gateway->asyncRemoveTopic(_clientID, _topicList, std::move(_callback));
    });
}

// Only the in-process gateway keeps a local node registry: the tars client branch is a no-op
// returning true (the remote gateway-service owns registration), preserving the former
// interface-default semantics for pro/max mode.
inline bool registerNode(GatewayHandle const& _handle, const std::string& _groupID,
    bcos::crypto::NodeIDPtr _nodeID, bcos::protocol::NodeType _nodeType,
    bcos::front::FrontService::Ptr _frontService,
    bcos::protocol::ProtocolInfo::ConstPtr _protocolInfo)
{
    return visitGateway(_handle, [&](auto const& _gateway) -> bool {
        if constexpr (requires {
                          _gateway->registerNode(_groupID, _nodeID, _nodeType, _frontService,
                              _protocolInfo);
                      })
        {
            return _gateway->registerNode(
                _groupID, _nodeID, _nodeType, _frontService, _protocolInfo);
        }
        else
        {
            return true;
        }
    });
}

// Bind the handle into the narrow std::function sink the front service sends through (the front
// library cannot reference the gateway types directly: gateway links front, so the binding lives
// here on the gateway side). The captured handle keeps the gateway object alive for as long as
// the front holds the sink.
inline bcos::front::FrontServiceGateway makeFrontServiceGateway(GatewayHandle _handle)
{
    bcos::front::FrontServiceGateway sink;
    sink.getGroupNodeInfo = [_handle](const std::string& _groupID) {
        return getGroupNodeInfo(_handle, _groupID);
    };
    sink.broadcastMessage = [_handle](uint16_t _type, std::string_view _groupID, int _moduleID,
                                const bcos::crypto::NodeID& _srcNodeID,
                                ::ranges::any_view<bytesConstRef, ::ranges::category::forward>
                                    _payloads) {
        return broadcastMessage(
            _handle, _type, _groupID, _moduleID, _srcNodeID, std::move(_payloads));
    };
    sink.sendMessageByNodeID = [_handle](const std::string& _groupID, int _moduleID,
                                   bcos::crypto::NodeIDPtr _srcNodeID,
                                   bcos::crypto::NodeIDPtr _dstNodeID,
                                   ::ranges::any_view<bytesConstRef, ::ranges::category::forward>
                                       _payloads) {
        return sendMessageByNodeID(_handle, _groupID, _moduleID, std::move(_srcNodeID),
            std::move(_dstNodeID), std::move(_payloads));
    };
    return sink;
}
}  // namespace bcos::gateway
