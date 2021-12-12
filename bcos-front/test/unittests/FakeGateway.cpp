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

using namespace bcos;
using namespace bcos::front;
using namespace bcos::front::test;
using namespace bcos::gateway;
/**
 * @brief: send message to a single node
 * @param _groupID: groupID
 * @param _nodeID: the receiver nodeID
 * @param _payload: message content
 * @param _options: option parameters
 * @param _callback: callback
 * @return void
 */
void FakeGateway::asyncSendMessageByNodeID(const std::string& _groupID,
    bcos::crypto::NodeIDPtr _srcNodeID, bcos::crypto::NodeIDPtr _dstNodeID, bytesConstRef _payload,
    bcos::gateway::ErrorRespFunc _errorRespFunc)
{
    m_frontService->onReceiveMessage(_groupID, _dstNodeID, _payload, _errorRespFunc);

    FRONT_LOG(DEBUG) << "[FakeGateway] asyncSendMessageByNodeID" << LOG_KV("groupID", _groupID)
                     << LOG_KV("nodeID", _srcNodeID->hex()) << LOG_KV("nodeID", _dstNodeID->hex());
}

/**
 * @brief: send message to multiple nodes
 * @param _groupID: groupID
 * @param _nodeIDs: the receiver nodeIDs
 * @param _payload: message content
 * @return void
 */
void FakeGateway::asyncSendMessageByNodeIDs(const std::string& _groupID,
    bcos::crypto::NodeIDPtr _srcNodeID, const bcos::crypto::NodeIDs& _dstNodeIDs,
    bytesConstRef _payload)
{
    if (!_dstNodeIDs.empty())
    {
        m_frontService->onReceiveMessage(
            _groupID, _srcNodeID, _payload, bcos::gateway::ErrorRespFunc());
    }

    FRONT_LOG(DEBUG) << "[FakeGateway] asyncSendMessageByNodeIDs" << LOG_KV("groupID", _groupID);
}

/**
 * @brief: send message to all nodes
 * @param _groupID: groupID
 * @param _payload: message content
 * @return void
 */
void FakeGateway::asyncSendBroadcastMessage(
    const std::string& _groupID, bcos::crypto::NodeIDPtr _srcNodeID, bytesConstRef _payload)
{
    m_frontService->onReceiveBroadcastMessage(_groupID, _srcNodeID, _payload, ErrorRespFunc());
    FRONT_LOG(DEBUG) << "asyncSendBroadcastMessage" << LOG_KV("groupID", _groupID);
}
