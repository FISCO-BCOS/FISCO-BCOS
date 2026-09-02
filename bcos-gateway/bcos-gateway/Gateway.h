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
 * @file Gateway.h
 * @author: octopus
 * @date 2021-04-19
 */

#pragma once

#include "bcos-framework/front/FrontServiceInterface.h"
#include "bcos-framework/gateway/GatewayInterface.h"
#include "bcos-framework/protocol/CommonError.h"
#include "bcos-gateway/Common.h"
#include "bcos-gateway/GatewayConfig.h"
#include "bcos-gateway/gateway/GatewayNodeManager.h"
#include "bcos-gateway/libamop/AMOPImpl.h"
#include "bcos-gateway/libp2p/Service.h"
#include "bcos-gateway/libratelimit/GatewayRateLimiter.h"
#include "bcos-utilities/BoostLog.h"
#include "filter/ReadOnlyFilter.h"
#include <range/v3/range/concepts.hpp>


namespace bcos::gateway
{
class Gateway : public GatewayInterface, public std::enable_shared_from_this<Gateway>
{
public:
    using Ptr = std::shared_ptr<Gateway>;
    Gateway(GatewayConfig::Ptr _gatewayConfig, P2PInterface::Ptr _p2pInterface,
        GatewayNodeManager::Ptr _gatewayNodeManager, bcos::amop::AMOPImpl::Ptr _amop,
        ratelimiter::GatewayRateLimiter::Ptr _gatewayRateLimiter,
        std::string _gatewayServiceName = "localGateway");
    ~Gateway() override;

    void start() override;
    void stop() override;

    /**
     * @brief: get connected peers
     * @return void
     */
    void asyncGetPeers(
        std::function<void(Error::Ptr, GatewayInfo::Ptr, GatewayInfosPtr)> _onGetPeers) override;
    /**
     * @brief: get nodeIDs from gateway
     * @param _groupID:
     * @param _onGetGroupNodeInfo: get nodeIDs callback
     * @return void
     */
    void asyncGetGroupNodeInfo(
        const std::string& _groupID, GetGroupNodeInfoFunc _onGetGroupNodeInfo) override;
    /**
     * @brief: send message to multiple nodes
     * @param _groupID: groupID
     * @param _moduleID: moduleID
     * @param _srcNodeID: the sender nodeID
     * @param _nodeIDs: the receiver nodeIDs
     * @param _payload: message payload
     * @return void
     */
    task::Task<void> broadcastMessage(uint16_t type, std::string_view groupID, int moduleID,
        const bcos::crypto::NodeID& srcNodeID,
        ::ranges::any_view<bytesConstRef, ::ranges::category::forward> payloads) override;

    /**
     * @brief: (coroutine, zero-copy) send message to a single node with retry across candidate
     *         p2p gateways. The payload views must be kept alive by the caller for the duration of
     *         the co_await; the coroutine resumes with nullptr on success or an Error::Ptr
     *         describing the failure.
     */
    task::Task<Error::Ptr> sendMessageByNodeID(const std::string& _groupID, int _moduleID,
        bcos::crypto::NodeIDPtr _srcNodeID, bcos::crypto::NodeIDPtr _dstNodeID,
        ::ranges::any_view<bytesConstRef, ::ranges::category::forward> _payloads) override;

    /**
     * @brief: receive p2p message
     * @param _groupID: groupID
     * @param _srcNodeID: the sender nodeID
     * @param _dstNodeID: the receiver nodeID
     * @param _payload: message content
     * @param _errorRespFunc: error func
     * @return void
     */
    virtual void onReceiveP2PMessage(const std::string& _groupID,
        bcos::crypto::NodeIDPtr _srcNodeID, bcos::crypto::NodeIDPtr _dstNodeID,
        bytesConstRef _payload, ErrorRespFunc _errorRespFunc = ErrorRespFunc());

    P2PInterface::Ptr p2pInterface() const;
    GatewayNodeManager::Ptr gatewayNodeManager();
    /**
     * @brief receive the latest group information notification from the GroupManagerInterface
     *
     * @param _groupInfo the latest group information
     */
    void asyncNotifyGroupInfo(
        bcos::group::GroupInfo::Ptr, std::function<void(Error::Ptr&&)>) override;

    /// for AMOP
    void asyncSendMessageByTopic(const std::string& _topic, bcos::bytesConstRef _data,
        std::function<void(bcos::Error::Ptr&&, int16_t, bytesConstRef)> _respFunc) override;
    void asyncSendBroadcastMessageByTopic(
        const std::string& _topic, bcos::bytesConstRef _data) override;

    void asyncSubscribeTopic(std::string const& _clientID, std::string const& _topicInfo,
        std::function<void(Error::Ptr&&)> _callback) override;

    void asyncRemoveTopic(std::string const& _clientID, std::vector<std::string> const& _topicList,
        std::function<void(Error::Ptr&&)> _callback) override;

    bcos::amop::AMOPImpl::Ptr amop();

    bool registerNode(const std::string& _groupID, bcos::crypto::NodeIDPtr _nodeID,
        bcos::protocol::NodeType _nodeType, bcos::front::FrontServiceInterface::Ptr _frontService,
        bcos::protocol::ProtocolInfo::ConstPtr _protocolInfo) override;

    virtual bool unregisterNode(const std::string& _groupID, std::string const& _nodeID);

    void enableReadOnlyMode();

protected:
    // for UT
    Gateway() = default;
    virtual void onReceiveP2PMessage(
        NetworkException const& _e, P2PSession::Ptr _session, std::shared_ptr<P2PMessage> _msg);

    /**
     * @brief: receive group broadcast message
     * @param _groupID: groupID
     * @param _srcNodeID: the sender nodeID
     * @param _payload: message content
     * @return void
     */
    virtual void onReceiveBroadcastMessage(
        NetworkException const& _e, P2PSession::Ptr _session, std::shared_ptr<P2PMessage> _msg);

    bool checkGroupInfo(bcos::group::GroupInfo::Ptr _groupInfo);

private:
    std::string m_gatewayServiceName;
    GatewayConfig::Ptr m_gatewayConfig;
    // p2p service interface
    P2PInterface::Ptr m_p2pInterface;
    // GatewayNodeManager
    GatewayNodeManager::Ptr m_gatewayNodeManager;
    bcos::amop::AMOPImpl::Ptr m_amop;

    // For rate limit
    ratelimiter::GatewayRateLimiter::Ptr m_gatewayRateLimiter;
    std::optional<ReadOnlyFilter> m_readonlyFilter;
};
}  // namespace bcos::gateway
