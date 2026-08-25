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
#include <atomic>
#include <coroutine>
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
    virtual void asyncSendMessageByNodeID(const std::string& _groupID, int _moduleID,
        bcos::crypto::NodeIDPtr _srcNodeID, bcos::crypto::NodeIDPtr _dstNodeID,
        bytesConstRef _payload, ErrorRespFunc _errorRespFunc) = 0;

    /**
     * @brief: send message to multiple nodes
     * @param _groupID: groupID
     * @param _moduleID: moduleID
     * @param _srcNodeID: the sender nodeID
     * @param _nodeIDs: the receiver nodeIDs
     * @param _payload: message content
     * @param _errorRespFunc: error func
     * @return void
     */
    virtual void asyncSendMessageByNodeIDs(const std::string& _groupID, int _moduleID,
        bcos::crypto::NodeIDPtr _srcNodeID, const bcos::crypto::NodeIDs& _dstNodeIDs,
        bytesConstRef _payload) = 0;

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
     * Default implementation: joins the payload views into a buffer and bridges to the borrowed
     * asyncSendMessageByNodeID. The completion state and the payload buffer are owned by the
     * completion callback (see the implementation below): the borrowed TARS client may keep
     * reading the payload after the callback returns, and it may even invoke the callback twice —
     * the implementation guards the resume/result delivery accordingly. The production Gateway
     * overrides it with a zero-copy coroutine implementation.
     *
     * @param _groupID: groupID
     * @param _moduleID: moduleID
     * @param _srcNodeID: the sender nodeID
     * @param _dstNodeID: the receiver nodeID
     * @param _payloads: message content (views, kept alive by the caller)
     */
    virtual task::Task<Error::Ptr> sendMessageByNodeID(const std::string& _groupID, int _moduleID,
        bcos::crypto::NodeIDPtr _srcNodeID, bcos::crypto::NodeIDPtr _dstNodeID,
        ::ranges::any_view<bytesConstRef, ::ranges::category::forward> _payloads)
    {
        // Both the payload buffer and the completion state are owned by shared_ptrs captured by the
        // completion callback, NOT by this coroutine frame: the borrowed TARS client may keep
        // reading the payload after the callback returns (e.g. a synchronous connection-check error
        // followed by an async send setup), and it may even invoke the callback twice (synchronously
        // for the connection check AND later for the async completion). The shared state survives
        // the frame so the second completion is detected instead of double-resuming a destroyed
        // coroutine.
        auto buffer = std::make_shared<bcos::bytes>();
        for (auto const& data : _payloads)
        {
            buffer->insert(buffer->end(), data.begin(), data.end());
        }
        struct SendAwaitable
        {
            struct CompletionState
            {
                std::atomic<bool> completed{false};
                std::coroutine_handle<> handle;
                Error::Ptr error;
            };

            GatewayInterface* m_self;
            // owned by the completion callback so it outlives this frame: asyncSendMessageByNodeID
            // takes the groupID by const-ref, and a synchronous completion (TARS connection check)
            // resumes (and destroys) the frame while the borrowed client is still using it
            std::shared_ptr<std::string> m_groupID;
            int m_moduleID;
            bcos::crypto::NodeIDPtr m_srcNodeID;
            bcos::crypto::NodeIDPtr m_dstNodeID;
            std::shared_ptr<bcos::bytes> m_buffer;
            std::shared_ptr<CompletionState> m_state;

            constexpr static bool await_ready() noexcept { return false; }
            void await_suspend(std::coroutine_handle<> _handle)
            {
                m_state->handle = _handle;
                auto state = m_state;
                auto buffer = m_buffer;
                auto groupID = m_groupID;
                // Materialize what the call arguments need (groupID / payload) BEFORE the lambda
                // argument below: the lambda's init-captures move buffer/groupID/state, and C++17
                // leaves the evaluation order of function arguments unspecified — if the lambda ran
                // first, *groupID / bcos::ref(*buffer) would dereference the moved-from (null)
                // shared_ptrs. The string stays alive because the lambda's captured shared_ptr
                // keeps it alive.
                std::string const& groupIDRef = *groupID;
                auto payload = bcos::ref(*buffer);
                m_self->asyncSendMessageByNodeID(groupIDRef, m_moduleID, m_srcNodeID, m_dstNodeID,
                    payload,
                    [state = std::move(state), buffer = std::move(buffer),
                        groupID = std::move(groupID)](bcos::Error::Ptr _error) mutable {
                        // The completion guard protects both the resume and the result delivery:
                        // the documented contract above says the borrowed TARS client may invoke
                        // this callback twice (synchronous connection check + async completion) —
                        // only the first completion may deliver the result, otherwise the caller
                        // is called back twice.
                        if (!state->completed.exchange(true))
                        {
                            state->error = std::move(_error);
                            // keep the payload buffer and groupID alive until after the resume: the
                            // borrowed caller may still be reading them on this stack
                            (void)buffer;
                            (void)groupID;
                            state->handle.resume();
                        }
                    });
            }
            Error::Ptr await_resume() { return std::move(m_state->error); }
        };
        SendAwaitable awaitable{this, std::make_shared<std::string>(_groupID), _moduleID,
            std::move(_srcNodeID), std::move(_dstNodeID), std::move(buffer),
            std::make_shared<SendAwaitable::CompletionState>()};
        co_return co_await awaitable;
    }

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
