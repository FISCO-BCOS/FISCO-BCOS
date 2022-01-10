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
 * @brief interface for front service module
 * @file FrontInterface.h
 * @author: octopus
 * @date 2021-04-19
 */
#pragma once
#include "bcos-framework/interfaces/crypto/KeyInterface.h"
#include "bcos-framework/interfaces/protocol/Protocol.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/Error.h"

namespace bcos
{
namespace front
{
using GetNodeIDsFunc =
    std::function<void(Error::Ptr _error, std::shared_ptr<const crypto::NodeIDs> _nodeIDs)>;
using ReceiveMsgFunc = std::function<void(Error::Ptr _error)>;
using ResponseFunc = std::function<void(bytesConstRef _respData)>;
using CallbackFunc = std::function<void(Error::Ptr _error, bcos::crypto::NodeIDPtr _nodeID,
    bytesConstRef _data, const std::string& _id, ResponseFunc _respFunc)>;

/**
 * @brief: the interface provided by the front service
 */
class FrontServiceInterface
{
public:
    using Ptr = std::shared_ptr<FrontServiceInterface>;
    FrontServiceInterface() = default;
    virtual ~FrontServiceInterface() {}

public:
    /**
     * @brief: start/stop service
     */
    virtual void start() = 0;
    virtual void stop() = 0;

public:
    /**
     * @brief: get nodeIDs from gateway
     * @param _getNodeIDsFunc: get nodeIDs callback
     * @return void
     */
    virtual void asyncGetNodeIDs(GetNodeIDsFunc _getNodeIDsFunc) = 0;
    /**
     * @brief: receive nodeIDs from gateway, call by gateway
     * @param _groupID: groupID
     * @param _nodeIDs: received nodeIDs
     * @return void
     */
    virtual void onReceiveNodeIDs(const std::string& _groupID,
        std::shared_ptr<const crypto::NodeIDs> _nodeIDs, ReceiveMsgFunc _receiveMsgCallback) = 0;

    /**
     * @brief: receive message from gateway, call by gateway
     * @param _groupID: groupID
     * @param _nodeID: the node send this message
     * @param _data: received message data
     * @return void
     */
    virtual void onReceiveMessage(const std::string& _groupID, bcos::crypto::NodeIDPtr _nodeID,
        bytesConstRef _data, ReceiveMsgFunc _receiveMsgCallback) = 0;

    /**
     * @brief: receive broadcast message from gateway, call by gateway
     * @param _groupID: groupID
     * @param _nodeID: the node send this message
     * @param _data: received message data
     * @return void
     */
    virtual void onReceiveBroadcastMessage(const std::string& _groupID,
        bcos::crypto::NodeIDPtr _nodeID, bytesConstRef _data,
        ReceiveMsgFunc _receiveMsgCallback) = 0;

    /**
     * @brief: send message to node
     * @param _moduleID: moduleID
     * @param _nodeID: the receiver nodeID
     * @param _data: message
     * @param _timeout: the timeout value of async function, in milliseconds.
     * @param _callback: callback
     * @return void
     */
    virtual void asyncSendMessageByNodeID(int _moduleID, bcos::crypto::NodeIDPtr _nodeID,
        bytesConstRef _data, uint32_t _timeout, CallbackFunc _callback) = 0;

    /**
     * @brief: send response
     * @param _id: the request id
     * @param _moduleID: moduleID
     * @param _nodeID: the receiver nodeID
     * @param _data: message
     * @return void
     */
    virtual void asyncSendResponse(const std::string& _id, int _moduleID,
        bcos::crypto::NodeIDPtr _nodeID, bytesConstRef _data,
        ReceiveMsgFunc _receiveMsgCallback) = 0;

    /**
     * @brief: send messages to multiple nodes
     * @param _moduleID: moduleID
     * @param _nodeIDs: the receiver nodeIDs
     * @param _data: message
     * @return void
     */
    virtual void asyncSendMessageByNodeIDs(int _moduleID,
        const std::vector<bcos::crypto::NodeIDPtr>& _nodeIDs, bytesConstRef _data) = 0;

    /**
     * @brief: send broadcast message
     * @param _moduleID: moduleID
     * @param _data:  message
     * @return void
     */
    virtual void asyncSendBroadcastMessage(uint16_t _type, int _moduleID, bytesConstRef _data) = 0;
};

}  // namespace front
}  // namespace bcos
