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
#include "bcos-crypto/interfaces/crypto/KeyInterface.h"
#include "bcos-framework/gateway/GroupNodeInfo.h"
#include "bcos-task/Task.h"
#include "bcos-task/Wait.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/Error.h"
#include <range/v3/view/any_view.hpp>
#include <range/v3/view/single.hpp>

namespace bcos::front
{
using GetGroupNodeInfoFunc = std::function<void(Error::Ptr, bcos::gateway::GroupNodeInfo::Ptr)>;
using ReceiveMsgFunc = std::function<void(Error::Ptr)>;
using ResponseFunc = std::function<void(bytesConstRef)>;
using CallbackFunc = std::function<void(
    Error::Ptr, bcos::crypto::NodeIDPtr, bytesConstRef, const std::string&, ResponseFunc)>;

/**
 * @brief: the interface provided by the front service
 */
class FrontServiceInterface
{
public:
    using Ptr = std::shared_ptr<FrontServiceInterface>;
    FrontServiceInterface() = default;
    virtual ~FrontServiceInterface() = default;
    FrontServiceInterface(const FrontServiceInterface&) = default;
    FrontServiceInterface(FrontServiceInterface&&) = default;
    FrontServiceInterface& operator=(const FrontServiceInterface&) = delete;
    FrontServiceInterface& operator=(FrontServiceInterface&&) = delete;

    /**
     * @brief: start/stop service
     */
    virtual void start() = 0;
    virtual void stop() = 0;

    /**
     * @brief: get groupNodeInfo from the gateway
     * @param _getGroupNodeInfoFunc: get groupNodeInfo callback
     * @return void
     */
    virtual void asyncGetGroupNodeInfo(GetGroupNodeInfoFunc _onGetGroupNodeInfo) = 0;
    virtual bcos::gateway::GroupNodeInfo::Ptr groupNodeInfo() const
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("Unimplemented!"));
    }
    /**
     * @brief: receive nodeIDs from gateway, call by gateway
     * @param _groupID: groupID
     * @param _groupNodeInfo: the groupNodeInfo
     * @return void
     */
    virtual void onReceiveGroupNodeInfo(const std::string& _groupID,
        bcos::gateway::GroupNodeInfo::Ptr _groupNodeInfo, ReceiveMsgFunc _receiveMsgCallback) = 0;

    /**
     * @brief: receive message from gateway, call by gateway
     * @param _groupID: groupID
     * @param _nodeID: the node send this message
     * @param _data: received message data
     * @return void
     */
    virtual void onReceiveMessage(const std::string& _groupID,
        const bcos::crypto::NodeIDPtr& _nodeID, bytesConstRef _data,
        ReceiveMsgFunc _receiveMsgCallback) = 0;

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

    virtual task::Task<void> broadcastMessage(
        uint16_t type, int moduleID, ::ranges::any_view<bytesConstRef> payloads) = 0;

    /**
     * @brief: (coroutine, zero-copy) send message to one node. The payload is passed as views that
     *         the caller must keep alive for the duration of the co_await. The module-level
     *         response (matched by the generated uuid) still arrives through the receive path and
     *         is delivered to _callback; _callback is also invoked with an error if the
     *         gateway-level send fails.
     *
     * Default implementation: joins the payload views into a buffer and bridges to the borrowed
     * asyncSendMessageByNodeID (correct for the tars client and test fakes).
     */
    virtual task::Task<void> sendMessageByNodeID(int _moduleID, bcos::crypto::NodeIDPtr _nodeID,
        ::ranges::any_view<bytesConstRef> _payloads, uint32_t _timeout, CallbackFunc _callback)
    {
        bcos::bytes buffer;
        for (auto const& data : _payloads)
        {
            buffer.insert(buffer.end(), data.begin(), data.end());
        }
        asyncSendMessageByNodeID(_moduleID, std::move(_nodeID), bcos::ref(buffer), _timeout,
            std::move(_callback));
        co_return;
    }

    /**
     * @brief broadcast an already-encoded message, taking ownership of the payload so the send can
     *        be deferred without copying the message body.
     *
     * The default implementation bridges to broadcastMessage on the caller thread (correct for the
     * tars/MAX client and test fakes, which have no in-process gateway lock to contend). The
     * production FrontService overrides this to dispatch the gateway send off the caller thread, so
     * a caller holding a lock (e.g. the PBFT consensus worker under m_mutex) is never coupled to
     * gateway session-lock contention under TLS-churn. The owned payload is kept alive for the
     * duration of the send.
     *
     * Constraint for new implementations: if your environment has in-process send-path lock
     * coupling (as AIR's gateway does), you MUST override this to dispatch the send off the caller
     * thread. The default (synchronous bridge) is safe only when the send cannot block on a lock
     * the caller may already hold.
     *
     * @param type: receiver node type
     * @param moduleID: moduleID
     * @param payload: already-encoded message body; ownership is transferred to the callee
     */
    virtual void asyncBroadcastMessageByOwnedPayload(
        uint16_t type, int moduleID, bytesPointer payload)
    {
        task::wait([](FrontServiceInterface* self, uint16_t _type, int _moduleID,
                       bytesPointer _payload) -> task::Task<void> {
            co_await self->broadcastMessage(
                _type, _moduleID, ::ranges::views::single(bcos::ref(*_payload)));
        }(this, type, moduleID, std::move(payload)));
    }

    /**
     * @brief send an already-encoded message to one node, taking ownership of the payload so the
     *        send can be deferred without the caller keeping the buffer alive.
     *
     * Same rationale as asyncBroadcastMessageByOwnedPayload: the production FrontService dispatches
     * the gateway send off the caller thread (PBFT under m_mutex must not contend the gateway
     * session lock; sendViewChange / sendRecoverResponse run there). Point-to-point sends encode
     * the wire frame anyway, so this is not zero-copy; owning the payload only keeps it alive
     * across the deferred encode. The default implementation bridges to asyncSendMessageByNodeID.
     *
     * @param moduleID: moduleID
     * @param nodeID: the receiver nodeID
     * @param payload: already-encoded message body; ownership is transferred to the callee
     */
    virtual void asyncSendMessageByNodeIDByOwnedPayload(
        int moduleID, bcos::crypto::NodeIDPtr nodeID, bytesPointer payload)
    {
        asyncSendMessageByNodeID(moduleID, std::move(nodeID), bcos::ref(*payload), 0, nullptr);
    }

    /**
     * @brief: get local protocol info
     * @return bcos::protocol::ProtocolInfo::ConstPtr
     */
    // virtual bcos::protocol::ProtocolInfo::ConstPtr getLocalProtocolInfo() const = 0;
};

}  // namespace bcos::front
