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
#include <bcos-framework/gateway/GatewayInterface.h>
#include <string>

#define GATEWAYCLIENT_LOG(LEVEL) BCOS_LOG(LEVEL) << "[GATEWAYCLIENT][INITIALIZER]"
#define GATEWAYCLIENT_BADGE "[GATEWAYCLIENT]"
namespace bcostars
{
class GatewayServiceClient : public bcos::gateway::GatewayInterface
{
public:
    GatewayServiceClient(bcostars::GatewayServicePrx _prx, std::string const& _serviceName,
        bcos::crypto::KeyFactory::Ptr _keyFactory);
    GatewayServiceClient(bcostars::GatewayServicePrx _prx, std::string const& _serviceName);
    virtual ~GatewayServiceClient() {}

    void setKeyFactory(bcos::crypto::KeyFactory::Ptr keyFactory);

    void asyncSendMessageByNodeID(const std::string& _groupID, int _moduleID,
        bcos::crypto::NodeIDPtr _srcNodeID, bcos::crypto::NodeIDPtr _dstNodeID,
        bcos::bytesConstRef _payload, bcos::gateway::ErrorRespFunc _errorRespFunc) override;

    void asyncGetPeers(std::function<void(
            bcos::Error::Ptr, bcos::gateway::GatewayInfo::Ptr, bcos::gateway::GatewayInfosPtr)>
            _callback) override;

    void asyncSendMessageByNodeIDs(const std::string& _groupID, int _moduleID,
        bcos::crypto::NodeIDPtr _srcNodeID, const bcos::crypto::NodeIDs& _dstNodeIDs,
        bcos::bytesConstRef _payload) override;

    void asyncSendBroadcastMessage(uint16_t _type, const std::string& _groupID, int _moduleID,
        bcos::crypto::NodeIDPtr _srcNodeID, bcos::bytesConstRef _payload) override;

    bcos::task::Task<void> broadcastMessage(uint16_t type, std::string_view groupID, int moduleID,
        const bcos::crypto::NodeID& srcNodeID,
        ::ranges::any_view<bcos::bytesConstRef> payloads) override;

    void asyncGetGroupNodeInfo(const std::string& _groupID,
        bcos::gateway::GetGroupNodeInfoFunc _onGetGroupNodeInfo) override;

    void asyncNotifyGroupInfo(bcos::group::GroupInfo::Ptr _groupInfo,
        std::function<void(bcos::Error::Ptr&&)> _callback) override;

    void asyncSendMessageByTopic(const std::string& _topic, bcos::bytesConstRef _data,
        std::function<void(bcos::Error::Ptr&&, int16_t, bcos::bytesConstRef)> _respFunc) override;

    void asyncSendBroadcastMessageByTopic(
        const std::string& _topic, bcos::bytesConstRef _data) override;

    void asyncSubscribeTopic(std::string const& _clientID, std::string const& _topicInfo,
        std::function<void(bcos::Error::Ptr&&)> _callback) override;

    void asyncRemoveTopic(std::string const& _clientID, std::vector<std::string> const& _topicList,
        std::function<void(bcos::Error::Ptr&&)> _callback) override;

    bcostars::GatewayServicePrx prx();

protected:
    void start() override {}
    void stop() override {}
    static bool shouldStopCall();

private:
    bcostars::GatewayServicePrx m_prx;
    std::string m_gatewayServiceName;
    // Note: only useful for asyncGetGroupNodeInfo
    bcos::crypto::KeyFactory::Ptr m_keyFactory;
    std::string const c_moduleName = "GatewayServiceClient";
    // AMOP timeout 40s
    const int c_amopTimeout = 40000;
    const int c_networkTimeout = 40000;
    static std::atomic<int64_t> s_tarsTimeoutCount;
    static const int64_t c_maxTarsTimeoutCount;
};
}  // namespace bcostars
