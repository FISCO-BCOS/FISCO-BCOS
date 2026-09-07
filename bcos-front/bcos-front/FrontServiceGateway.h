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
 * @brief narrow type-erased sink over the gateway entry points the front service calls
 * @file FrontServiceGateway.h
 */
#pragma once
#include <bcos-crypto/interfaces/crypto/KeyInterface.h>
#include <bcos-framework/gateway/GroupNodeInfo.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Error.h>
#include <functional>
#include <range/v3/view/any_view.hpp>
#include <string>
#include <string_view>
#include <tuple>

namespace bcos::front
{
// The three coroutine calls FrontService makes into its gateway, as plain std::functions bound by
// the assembly side (libinitializer binds the concrete bcos::gateway::Gateway, or the
// GatewayServiceClient in pro mode, via bcos-gateway's makeFrontServiceGateway(); tests bind their
// fakes the same way). Type erasure instead of a concrete gateway pointer keeps the front library
// free of any bcos-gateway dependency — the gateway library links front (FrontServiceHandle /
// registerNode), so a concrete type here would close a front<->gateway link cycle.
//
// The coroutine reference-lifetime contract is unchanged from the former interface: a parameter
// passed by reference (groupID) is held by the callee coroutine frame across suspensions, so the
// caller must keep it alive until the returned task completes — do not pass a temporary.
struct FrontServiceGateway
{
    std::function<task::Task<std::tuple<Error::Ptr, bcos::gateway::GroupNodeInfo::Ptr>>(
        const std::string& groupID)>
        getGroupNodeInfo;
    std::function<task::Task<void>(uint16_t type, std::string_view groupID, int moduleID,
        const bcos::crypto::NodeID& srcNodeID,
        ::ranges::any_view<bytesConstRef, ::ranges::category::forward> payloads)>
        broadcastMessage;
    std::function<task::Task<Error::Ptr>(const std::string& groupID, int moduleID,
        bcos::crypto::NodeIDPtr srcNodeID, bcos::crypto::NodeIDPtr dstNodeID,
        ::ranges::any_view<bytesConstRef, ::ranges::category::forward> payloads)>
        sendMessageByNodeID;
};
}  // namespace bcos::front
