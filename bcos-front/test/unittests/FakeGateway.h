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
 * @file FakeGateway.h
 * @author: octopus
 * @date 2021-04-27
 */

#pragma once

#include <bcos-framework/front/FrontServiceInterface.h>
#include <bcos-framework/gateway/GatewayInterface.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/ThreadPool.h>
#include <boost/asio.hpp>
#include <boost/core/ignore_unused.hpp>

namespace bcos
{
namespace front
{
namespace test
{
class FakeGateway : public gateway::GatewayInterface,
                    public std::enable_shared_from_this<FakeGateway>
{
public:
    virtual ~FakeGateway() {}

public:
    std::shared_ptr<FrontServiceInterface> m_frontService;
    void setFrontService(std::shared_ptr<FrontServiceInterface> _frontService)
    {
        m_frontService = _frontService;
    }

public:
    /**
     * @brief: start/stop service
     */
    void start() override {}
    void stop() override {}
    void asyncGetPeers(std::function<void(
            Error::Ptr, bcos::gateway::GatewayInfo::Ptr, bcos::gateway::GatewayInfosPtr)>) override
    {}
    /**
     * @brief: get nodeIDs from gateway
     * @param _groupID:
     * @param _onGetGroupNodeInfo: get nodeIDs callback
     * @return void
     */
    void asyncGetGroupNodeInfo(
        const std::string& _groupID, GetGroupNodeInfoFunc _onGetGroupNodeInfo) override
    {
        boost::ignore_unused(_groupID, _onGetGroupNodeInfo);
    }

    /**
     * @brief: send message to a single node
     * @param _groupID: groupID
     * @param _srcNodeID: the sender nodeID
     * @param _dstNodeID: the receiver nodeID
     * @param _payload: message content
     * @return void
     */
    void asyncSendMessageByNodeID(const std::string& _groupID, bcos::crypto::NodeIDPtr _srcNodeID,
        bcos::crypto::NodeIDPtr _dstNodeID, bytesConstRef _payload,
        bcos::gateway::ErrorRespFunc _errorRespFunc) override;

    /**
     * @brief: send message to multiple nodes
     * @param _groupID: groupID
     * @param _srcNodeID: the sender nodeID
     * @param _nodeIDs: the receiver nodeIDs
     * @param _payload: message content
     * @return void
     */
    void asyncSendMessageByNodeIDs(const std::string& _groupID, bcos::crypto::NodeIDPtr _srcNodeID,
        const bcos::crypto::NodeIDs& _dstNodeIDs, bytesConstRef _payload) override;

    /**
     * @brief: send message to all nodes
     * @param _groupID: groupID
     * @param _srcNodeID: the sender nodeID
     * @param _payload: message content
     * @return void
     */
    void asyncSendBroadcastMessage(uint16_t, const std::string& _groupID,
        bcos::crypto::NodeIDPtr _srcNodeID, bytesConstRef _payload) override;

    void asyncNotifyGroupInfo(
        bcos::group::GroupInfo::Ptr, std::function<void(Error::Ptr&&)>) override
    {}

    void asyncSendMessageByTopic(const std::string&, bcos::bytesConstRef,
        std::function<void(bcos::Error::Ptr&&, int16_t, bytesPointer)>) override
    {}
    void asyncSendBroadbastMessageByTopic(const std::string&, bcos::bytesConstRef) override {}

    void asyncSubscribeTopic(
        std::string const&, std::string const&, std::function<void(Error::Ptr&&)>) override
    {}
    void asyncRemoveTopic(std::string const&, std::vector<std::string> const&,
        std::function<void(Error::Ptr&&)>) override
    {}
};

}  // namespace test
}  // namespace front
}  // namespace bcos
