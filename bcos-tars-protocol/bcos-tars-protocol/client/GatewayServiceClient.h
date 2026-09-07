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
 * @file GatewayServiceClient.h
 * @author: ancelmo
 * @date 2021-04-20
 */

#pragma once

#include "bcos-tars-protocol/tars/GatewayService.h"
#include <bcos-crypto/interfaces/crypto/KeyFactory.h>
#include <bcos-framework/gateway/GatewayTypeDef.h>
#include <bcos-framework/gateway/GroupNodeInfo.h>
#include <bcos-framework/multigroup/GroupInfo.h>
#include <bcos-task/Task.h>
#include <range/v3/view/any_view.hpp>
#include <string>
#include <bcos-utilities/BoostLog.h>

#define GATEWAYCLIENT_LOG(LEVEL) BCOS_LOG(LEVEL) << "[GATEWAYCLIENT][INITIALIZER]"
#define GATEWAYCLIENT_BADGE "[GATEWAYCLIENT]"
namespace bcostars
{
// Standalone concrete client for the remote (pro/max-mode) gateway-service; the local
// counterpart is bcos::gateway::Gateway and consumers hold the statically-dispatched
// bcos::gateway::GatewayHandle over the two (bcos-gateway/gateway/GatewayHandle.h). Method
// signatures mirror Gateway's exactly so the variant dispatch compiles against both.
class GatewayServiceClient
{
public:
    using Ptr = std::shared_ptr<GatewayServiceClient>;

    GatewayServiceClient(bcostars::GatewayServicePrx _prx, std::string const& _serviceName,
        bcos::crypto::KeyFactory::Ptr _keyFactory);
    GatewayServiceClient(bcostars::GatewayServicePrx _prx, std::string const& _serviceName);
    ~GatewayServiceClient();

    void setKeyFactory(bcos::crypto::KeyFactory::Ptr keyFactory);

    bcos::task::Task<std::tuple<bcos::Error::Ptr, bcos::gateway::GatewayInfo::Ptr,
        bcos::gateway::GatewayInfosPtr>>
    getPeers();

    // (coroutine) send message to a single node by awaiting the gateway-service RPC
    bcos::task::Task<bcos::Error::Ptr> sendMessageByNodeID(const std::string& _groupID,
        int _moduleID, bcos::crypto::NodeIDPtr _srcNodeID, bcos::crypto::NodeIDPtr _dstNodeID,
        ::ranges::any_view<bcos::bytesConstRef, ::ranges::category::forward> _payloads);

    bcos::task::Task<void> broadcastMessage(uint16_t type, std::string_view groupID, int moduleID,
        const bcos::crypto::NodeID& srcNodeID,
        ::ranges::any_view<bcos::bytesConstRef, ::ranges::category::forward> payloads);

    bcos::task::Task<std::tuple<bcos::Error::Ptr, bcos::gateway::GroupNodeInfo::Ptr>>
    getGroupNodeInfo(const std::string& _groupID);

    void asyncNotifyGroupInfo(bcos::group::GroupInfo::Ptr _groupInfo,
        std::function<void(bcos::Error::Ptr&&)> _callback);

    bcos::task::Task<std::tuple<bcos::Error::Ptr, int16_t, bcos::bytes>> sendMessageByTopic(
        const std::string& _topic, bcos::bytesConstRef _data);

    bcos::task::Task<void> sendBroadcastMessageByTopic(
        const std::string& _topic, bcos::bytesConstRef _data);

    void asyncSubscribeTopic(std::string const& _clientID, std::string const& _topicInfo,
        std::function<void(bcos::Error::Ptr&&)> _callback);

    void asyncRemoveTopic(std::string const& _clientID, std::vector<std::string> const& _topicList,
        std::function<void(bcos::Error::Ptr&&)> _callback);

    bcostars::GatewayServicePrx prx();

    // no-ops: the client has no local lifecycle (kept callable through GatewayHandle)
    void start();
    void stop();

private:
    static bool shouldStopCall();

private:
    bcostars::GatewayServicePrx m_prx;
    std::string m_gatewayServiceName;
    // Note: only useful for getGroupNodeInfo
    bcos::crypto::KeyFactory::Ptr m_keyFactory;
    std::string const c_moduleName = "GatewayServiceClient";
    // AMOP timeout 40s
    const int c_amopTimeout = 40000;
    const int c_networkTimeout = 40000;
    static std::atomic<int64_t> s_tarsTimeoutCount;
    static const int64_t c_maxTarsTimeoutCount;
};
}  // namespace bcostars
