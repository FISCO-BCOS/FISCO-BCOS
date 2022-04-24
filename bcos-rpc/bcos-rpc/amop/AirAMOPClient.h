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
 * @brief AMOP client
 * @file AirAMOPClient.h
 * @author: yujiechen
 * @date 2021-10-28
 */
#pragma once
#include "AMOPClient.h"
#include <bcos-boostssl/interfaces/MessageFace.h>
#include <bcos-boostssl/websocket/WsSession.h>

namespace bcos
{
namespace rpc
{
class AirAMOPClient : public AMOPClient
{
public:
    using Ptr = std::shared_ptr<AirAMOPClient>;
    AirAMOPClient(std::shared_ptr<boostssl::ws::WsService> _wsService,
        std::shared_ptr<bcos::boostssl::ws::WsMessageFactory> _wsMessageFactory,
        std::shared_ptr<bcos::protocol::AMOPRequestFactory> _requestFactory,
        bcos::gateway::GatewayInterface::Ptr _gateway)
      : AMOPClient(_wsService, _wsMessageFactory, _requestFactory, _gateway, "localGateway")
    {}

    // Note: must with empty implementation to in case of start the m_gatewayStatusDetector
    void start() override { m_gatewayActivated.store(true); }

    bool onGatewayInactivated(
        std::shared_ptr<boostssl::MessageFace>, std::shared_ptr<boostssl::ws::WsSession>) override
    {
        return false;
    }

    bool gatewayInactivated() override { return false; }

protected:
    void subscribeTopicToAllNodes() override
    {
        auto topicInfo = generateTopicInfo();
        AMOP_CLIENT_LOG(INFO) << LOG_DESC("subscribeTopicToAllNodes")
                              << LOG_KV("topicInfo", topicInfo);
        m_gateway->asyncSubscribeTopic(m_clientID, topicInfo, [](Error::Ptr&& _error) {
            if (_error)
            {
                BCOS_LOG(WARNING) << LOG_DESC("asyncSubScribeTopic error")
                                  << LOG_KV("code", _error->errorCode())
                                  << LOG_KV("msg", _error->errorMessage());
            }
        });
    }

    void removeTopicFromAllNodes(std::vector<std::string> const& _topicsToRemove) override
    {
        m_gateway->asyncRemoveTopic(
            m_clientID, _topicsToRemove, [_topicsToRemove](Error::Ptr&& _error) {
                BCOS_LOG(INFO) << LOG_DESC("asyncRemoveTopic")
                               << LOG_KV("removedSize", _topicsToRemove.size())
                               << LOG_KV("code", _error ? _error->errorCode() : 0)
                               << LOG_KV("msg", _error ? _error->errorMessage() : "");
            });
    }
};
}  // namespace rpc
}  // namespace bcos