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
#include "../../interfaces/crypto/KeyInterface.h"
#include "../../interfaces/multigroup/GroupInfo.h"
#include "GatewayTypeDef.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/Error.h"

namespace bcos
{
namespace gateway
{
using ErrorRespFunc = std::function<void(Error::Ptr)>;
using PeerRespFunc = std::function<void(Error::Ptr, const std::string&)>;
using GetNodeIDsFunc =
    std::function<void(Error::Ptr _error, std::shared_ptr<const crypto::NodeIDs> _nodeIDs)>;

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

public:
    /**
     * @brief: get nodeIDs from gateway
     * @param: _groupID
     * @param _getNodeIDsFunc: get nodeIDs callback
     * @return void
     */
    virtual void asyncGetNodeIDs(const std::string& _groupID, GetNodeIDsFunc _getNodeIDsFunc) = 0;
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
     * @param _srcNodeID: the sender nodeID
     * @param _dstNodeID: the receiver nodeID
     * @param _payload: message content
     * @return void
     */
    virtual void asyncSendMessageByNodeID(const std::string& _groupID,
        bcos::crypto::NodeIDPtr _srcNodeID, bcos::crypto::NodeIDPtr _dstNodeID,
        bytesConstRef _payload, ErrorRespFunc _errorRespFunc) = 0;

    /**
     * @brief: send message to multiple nodes
     * @param _groupID: groupID
     * @param _srcNodeID: the sender nodeID
     * @param _nodeIDs: the receiver nodeIDs
     * @param _payload: message content
     * @param _errorRespFunc: error func
     * @return void
     */
    virtual void asyncSendMessageByNodeIDs(const std::string& _groupID,
        bcos::crypto::NodeIDPtr _srcNodeID, const bcos::crypto::NodeIDs& _dstNodeIDs,
        bytesConstRef _payload) = 0;

    /**
     * @brief: send message to all nodes
     * @param _groupID: groupID
     * @param _srcNodeID: the sender nodeID
     * @param _payload: message content
     * @return void
     */
    virtual void asyncSendBroadcastMessage(const std::string& _groupID,
        bcos::crypto::NodeIDPtr _srcNodeID, bytesConstRef _payload) = 0;

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
        std::function<void(bcos::Error::Ptr&&, int16_t, bytesPointer)> _respFunc) = 0;
    virtual void asyncSendBroadbastMessageByTopic(
        const std::string& _topic, bcos::bytesConstRef _data) = 0;

    virtual void asyncSubscribeTopic(std::string const& _clientID, std::string const& _topicInfo,
        std::function<void(Error::Ptr&&)> _callback) = 0;
    virtual void asyncRemoveTopic(std::string const& _clientID,
        std::vector<std::string> const& _topicList,
        std::function<void(Error::Ptr&&)> _callback) = 0;
};

}  // namespace gateway
}  // namespace bcos
