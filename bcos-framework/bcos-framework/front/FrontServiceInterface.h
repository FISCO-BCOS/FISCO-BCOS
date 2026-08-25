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
#include <atomic>
#include <coroutine>
#include <functional>
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
 * @brief: the result of a module-level point-to-point send: the peer's response, or a gateway send
 *         failure / timeout (error non-null). payload is an owned copy so it outlives the front
 *         receive-path buffer it was decoded from; respond (optional) lets the caller send a
 *         follow-up response to the peer over the same request uuid.
 */
struct SendResult
{
    bcos::Error::Ptr error;                     // non-null on gateway send failure or timeout
    bcos::crypto::NodeIDPtr nodeID;             // the node that sent the response
    bcos::bytes payload;                        // owned copy of the response body
    std::string uuid;                           // request uuid (echoed by the peer response)
    std::function<void(bytesConstRef)> respond; // optional follow-up response to the peer
};

/**
 * @brief: internal awaitable that bridges a module-level response callback (fired on response /
 *         timeout / gateway failure, on the front's io-thread pool) to a co_await. Race-safe:
 *         whichever of the callback and the suspension wins, the coroutine is resumed exactly once
 *         and the result is delivered exactly once.
 */
class SendResponseAwaitable
{
public:
    struct State
    {
        std::atomic<bool> done{false};
        std::coroutine_handle<> handle{nullptr};
        SendResult result;
        Mutex mutex;
    };
    using StatePtr = std::shared_ptr<State>;

    explicit SendResponseAwaitable(StatePtr state) : m_state(std::move(state)) {}

    bool await_ready() const noexcept { return m_state->done.load(std::memory_order_acquire); }

    void await_suspend(std::coroutine_handle<> handle)
    {
        std::lock_guard lock(m_state->mutex);
        if (m_state->done.load(std::memory_order_acquire))
        {
            // the callback already completed before we suspended: resume immediately
            handle.resume();
            return;
        }
        m_state->handle = handle;
    }

    SendResult await_resume() { return std::move(m_state->result); }

    // Called by the response/timeout/gateway-failure callback (on its own thread). Completes the
    // wait exactly once; if the coroutine has not suspended yet, await_suspend observes done and
    // resumes itself.
    static void complete(const StatePtr& state, SendResult result)
    {
        std::coroutine_handle<> handle;
        {
            std::lock_guard lock(state->mutex);
            if (state->done.exchange(true, std::memory_order_acq_rel))
            {
                return;
            }
            state->result = std::move(result);
            handle = state->handle;
        }
        if (handle)
        {
            handle.resume();
        }
    }

private:
    StatePtr m_state;
};

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
     * @brief: (coroutine, zero-copy) send message to one node and await the module-level response.
     *         The payload is passed as views that the caller must keep alive for the duration of
     *         the co_await. co_await resumes when the peer's response arrives, the timeout
     *         (_timeout > 0) expires, or the gateway-level send fails — the result carries the
     *         response (error non-null on timeout / gateway failure). With _timeout == 0 the send
     *         is fire-and-forget: no response is expected and the coroutine returns as soon as the
     *         gateway send completes. Callers that want the pure async form can wrap the co_await
     *         in task::wait, passing any captured state as coroutine parameters (the recommended
     *         pattern across the send path).
     *
     * The payloads must be at least forward ranges: the default bridge joins them into one buffer
     * (single pass), but overrides may iterate them multiple times (e.g. a retry loop).
     *
     * Default implementation: joins the payload views into a buffer owned by the completion
     * callback (NOT this coroutine frame) and bridges to the borrowed asyncSendMessageByNodeID —
     * the TARS/MAX front client may keep reading the payload after the callback returns (e.g. a
     * synchronous connection-check error followed by an async send setup), so the buffer must
     * outlive the frame. This mirrors the GatewayInterface default bridge, which the gateway-side
     * review rounds established against the same TARS client contract.
     */
    virtual task::Task<SendResult> sendMessageByNodeID(int _moduleID,
        bcos::crypto::NodeIDPtr _nodeID,
        ::ranges::any_view<bytesConstRef, ::ranges::category::forward> _payloads,
        uint32_t _timeout)
    {
        // The payload views are joined into a buffer owned by a shared_ptr captured by the
        // completion callback, NOT this coroutine frame: the borrowed TARS/MAX front client may
        // keep reading the payload after the callback returns.
        auto buffer = std::make_shared<bcos::bytes>();
        for (auto const& data : _payloads)
        {
            buffer->insert(buffer->end(), data.begin(), data.end());
        }
        auto payload = bcos::ref(*buffer);

        if (_timeout == 0)
        {
            // fire-and-forget: no module-level response is expected. Keep the buffer owned by an
            // empty completion callback until the borrowed client is done with it, and return
            // once the send is dispatched.
            asyncSendMessageByNodeID(_moduleID, std::move(_nodeID), payload, 0,
                [buffer = std::move(buffer)](bcos::Error::Ptr, bcos::crypto::NodeIDPtr,
                    bytesConstRef, const std::string&, ResponseFunc) mutable { (void)buffer; });
            co_return SendResult{};
        }

        auto state = std::make_shared<SendResponseAwaitable::State>();
        asyncSendMessageByNodeID(_moduleID, std::move(_nodeID), payload, _timeout,
            [state, buffer = std::move(buffer)](bcos::Error::Ptr _error,
                bcos::crypto::NodeIDPtr _nodeID, bytesConstRef _data, const std::string& _id,
                ResponseFunc _resp) mutable {
                // keep the payload buffer alive until after the callback returns: the borrowed
                // caller may still be reading it on this stack
                (void)buffer;
                SendResult result;
                result.error = std::move(_error);
                result.nodeID = std::move(_nodeID);
                result.payload.assign(_data.begin(), _data.end());
                result.uuid = _id;
                result.respond = std::move(_resp);
                SendResponseAwaitable::complete(state, std::move(result));
            });
        co_return co_await SendResponseAwaitable{std::move(state)};
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
