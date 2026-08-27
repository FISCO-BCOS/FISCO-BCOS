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
 * @brief interface for Gateway module
 * @file GatewayInterface.h
 * @author: octopus
 * @date 2021-04-19
 */
#pragma once
#include "GatewayTypeDef.h"
#include "bcos-framework/front/FrontServiceInterface.h"
#include "bcos-framework/multigroup/GroupInfo.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-framework/protocol/ProtocolInfo.h"
#include "bcos-task/Task.h"
#include <bcos-crypto/interfaces/crypto/KeyInterface.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Error.h>
#include <memory>
#include <range/v3/view/any_view.hpp>

namespace bcos
{
namespace gateway
{
using ErrorRespFunc = std::function<void(Error::Ptr)>;
using PeerRespFunc = std::function<void(Error::Ptr, const std::string&)>;
using GetGroupNodeInfoFunc =
    std::function<void(Error::Ptr _error, bcos::gateway::GroupNodeInfo::Ptr _nodeIDs)>;

/**
 * @brief: A list of interfaces provided by the gateway which are called by the front service.
 */
class GatewayInterface
{
public:
    using Ptr = std::shared_ptr<GatewayInterface>;
    GatewayInterface() = default;
    virtual ~GatewayInterface() {}

    /**
     * @brief: start/stop service
     */
    virtual void start() = 0;
    virtual void stop() = 0;

    /**
     * @brief: get nodeIDs from gateway
     * @param: _groupID
     * @param _getGroupNodeInfoFunc: get nodeIDs callback
     * @return void
     */
    virtual void asyncGetGroupNodeInfo(
        const std::string& _groupID, GetGroupNodeInfoFunc _getGroupNodeInfoFunc) = 0;
    /**
     * @brief: get connected peers
     * @param _callback:
     * @return void
     */
    virtual void asyncGetPeers(
        std::function<void(Error::Ptr, GatewayInfo::Ptr, GatewayInfosPtr)> _callback) = 0;
    /**
     * @brief: send message to a single node
     * @param _groupID: groupID
     * @param _moduleID: moduleID
     * @param _srcNodeID: the sender nodeID
     * @param _dstNodeID: the receiver nodeID
     * @param _payload: message content
     * @return void
     */

    virtual task::Task<void> broadcastMessage(uint16_t type, std::string_view groupID, int moduleID,
        const bcos::crypto::NodeID& srcNodeID,
        ::ranges::any_view<bytesConstRef, ::ranges::category::forward> payloads) = 0;

    /**
     * @brief: (coroutine) send message to a single node, zero-copy. The payload is passed as views
     *         that the caller must keep alive for the duration of the co_await. The coroutine
     *         completes once the peer gateway acknowledges the message (or the retries are
     *         exhausted / a terminal error occurred); it returns nullptr on success or an
     *         Error::Ptr describing the failure.
     *
     * @param _groupID: groupID
     * @param _moduleID: moduleID
     * @param _srcNodeID: the sender nodeID
     * @param _dstNodeID: the receiver nodeID
     * @param _payloads: message content (views, kept alive by the caller)
     */
    virtual task::Task<Error::Ptr> sendMessageByNodeID(const std::string& _groupID, int _moduleID,
        bcos::crypto::NodeIDPtr _srcNodeID, bcos::crypto::NodeIDPtr _dstNodeID,
        ::ranges::any_view<bytesConstRef, ::ranges::category::forward> _payloads) = 0;

    /// multi-group related interfaces

    /**
     * @brief receive the latest group information notification from the GroupManagerInterface
     *
     * @param _groupInfo the latest group information
     */
    virtual void asyncNotifyGroupInfo(
        bcos::group::GroupInfo::Ptr _groupInfo, std::function<void(Error::Ptr&&)>) = 0;

    /// for AMOP
    virtual void asyncSendMessageByTopic(const std::string& _topic, bcos::bytesConstRef _data,
        std::function<void(bcos::Error::Ptr&&, int16_t, bytesConstRef)> _respFunc) = 0;
    virtual void asyncSendBroadcastMessageByTopic(
        const std::string& _topic, bcos::bytesConstRef _data) = 0;

    virtual void asyncSubscribeTopic(std::string const& _clientID, std::string const& _topicInfo,
        std::function<void(Error::Ptr&&)> _callback) = 0;
    virtual void asyncRemoveTopic(std::string const& _clientID,
        std::vector<std::string> const& _topicList,
        std::function<void(Error::Ptr&&)> _callback) = 0;

    // for the air-mode node
    virtual bool registerNode(const std::string&, bcos::crypto::NodeIDPtr, bcos::protocol::NodeType,
        bcos::front::FrontServiceInterface::Ptr, bcos::protocol::ProtocolInfo::ConstPtr)
    {
        return true;
    }
};

}  // namespace gateway
}  // namespace bcos
