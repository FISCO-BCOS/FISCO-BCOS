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
 * @brief Gateway fake implementation
 * @file FakeGateway.cpp
 * @author: octopus
 * @date 2021-04-27
 */

#include "FakeGateway.h"
#include <bcos-front/Common.h>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/join.hpp>

using namespace bcos;
using namespace bcos::front;
using namespace bcos::front::test;
using namespace bcos::gateway;
/**
 * @brief: send message to a single node
 * @param _groupID: groupID
 * @param _moduleID: moduleID
 * @param _nodeID: the receiver nodeID
 * @param _payload: message content
 * @param _options: option parameters
 * @param _callback: callback
 * @return void
 */
bcos::task::Task<bcos::Error::Ptr> bcos::front::test::FakeGateway::sendMessageByNodeID(
    const std::string& _groupID, int _moduleID, bcos::crypto::NodeIDPtr _srcNodeID,
    bcos::crypto::NodeIDPtr _dstNodeID,
    ::ranges::any_view<bytesConstRef, ::ranges::category::forward> _payloads)
{
    bcos::bytes buffer;
    for (auto const& data : _payloads)
    {
        buffer.insert(buffer.end(), data.begin(), data.end());
    }
    if (m_sendError)
    {
        co_return m_sendError;
    }
    if (auto frontService = m_frontService.lock())
    {
        frontService->onReceiveMessage(
            _groupID, _dstNodeID, bcos::ref(buffer), bcos::gateway::ErrorRespFunc());
    }

    FRONT_LOG(DEBUG) << "[FakeGateway] sendMessageByNodeID" << LOG_KV("groupID", _groupID)
                     << LOG_KV("nodeID", _srcNodeID->hex()) << LOG_KV("nodeID", _dstNodeID->hex());
    co_return nullptr;
}
bcos::task::Task<void> bcos::front::test::FakeGateway::broadcastMessage(uint16_t type,
    std::string_view groupID, int moduleID, const bcos::crypto::NodeID& srcNodeID,
    ::ranges::any_view<bytesConstRef, ::ranges::category::forward> payloads)
{
    auto data = ::ranges::views::join(payloads) | ::ranges::to<bcos::bytes>();
    auto nodeIDPtr = std::shared_ptr<bcos::crypto::NodeID>(
        const_cast<bcos::crypto::NodeID*>(std::addressof(srcNodeID)), [](auto* ptr) {});
    if (auto frontService = m_frontService.lock())
    {
        frontService->onReceiveBroadcastMessage(
            std::string{groupID}, nodeIDPtr, ref(data), ErrorRespFunc());
    }
    FRONT_LOG(DEBUG) << "asyncSendBroadcastMessage" << LOG_KV("groupID", groupID);
    co_return;
};