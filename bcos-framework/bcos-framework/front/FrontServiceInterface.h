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
 *
 * Publication order: complete() writes State::result BEFORE publishing State::done (release),
 * and await_ready()/await_suspend() read done with acquire — so a reader that observes done ==
 * true is guaranteed to see the fully-written result. The idempotency guard stays under the mutex;
 * the lock itself only serializes complete() vs await_suspend().
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

    // Lock-free fast path: pairs with complete()'s release store — observing done == true implies
    // result is fully written, so await_resume() may move it without the lock.
    bool await_ready() const noexcept { return m_state->done.load(std::memory_order_acquire); }

    void await_suspend(std::coroutine_handle<> handle)
    {
        // Take a local reference to the state before touching it: the coroutine chain resumed
        // below may destroy the last State reference (this awaitable's m_state), so the mutex must
        // stay alive until after the unlock. This mirrors complete()'s "take handle under the
        // lock, resume outside it" shape.
        auto state = m_state;
        {
            std::lock_guard lock(state->mutex);
            if (!state->done.load(std::memory_order_acquire))
            {
                state->handle = handle;
                return;
            }
        }
        // the callback already completed before we suspended: resume outside the lock
        handle.resume();
    }

    SendResult await_resume() { return std::move(m_state->result); }

    // Called by the response/timeout/gateway-failure callback (on its own thread). Completes the
    // wait exactly once; if the coroutine has not suspended yet, await_suspend observes done and
    // resumes itself. result is written BEFORE done is published (release) so the lock-free
    // await_ready() fast path never races with this write.
    static void complete(const StatePtr& state, SendResult result)
    {
        std::coroutine_handle<> handle;
        {
            std::lock_guard lock(state->mutex);
            if (state->done.load(std::memory_order_relaxed))
            {
                return;
            }
            state->result = std::move(result);
            state->done.store(true, std::memory_order_release);
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
 *
 * enable_shared_from_this lets the owned-payload bridges (asyncBroadcastMessageByOwnedPayload /
 * asyncSendMessageByNodeIDByOwnedPayload) pass an owning Ptr as the coroutine parameter, so the
 * front object (FrontService / FrontServiceClient / test fakes, all shared_ptr-owned) stays alive
 * for the whole detached send instead of holding a raw `this`.
 */
class FrontServiceInterface : public std::enable_shared_from_this<FrontServiceInterface>
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

    virtual task::Task<void> broadcastMessage(
        uint16_t type, int moduleID,
        ::ranges::any_view<bytesConstRef, ::ranges::category::forward> payloads) = 0;

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
     * The payloads must be at least forward ranges: implementations may iterate them multiple
     * times (e.g. a retry loop).
     *
     * @param _moduleID: moduleID
     * @param _nodeID: the receiver nodeID
     * @param _payloads: message content (views, kept alive by the caller)
     * @param _timeout: the module-response timeout in milliseconds; 0 = fire-and-forget
     */
    virtual task::Task<SendResult> sendMessageByNodeID(int _moduleID,
        bcos::crypto::NodeIDPtr _nodeID,
        ::ranges::any_view<bytesConstRef, ::ranges::category::forward> _payloads,
        uint32_t _timeout) = 0;

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
        task::wait([](FrontServiceInterface::Ptr self, uint16_t _type, int _moduleID,
                       bytesPointer _payload) -> task::Task<void> {
            co_await self->broadcastMessage(
                _type, _moduleID, ::ranges::views::single(bcos::ref(*_payload)));
        }(shared_from_this(), type, moduleID, std::move(payload)));
    }

    /**
     * @brief send an already-encoded message to one node, taking ownership of the payload so the
     *        send can be deferred without the caller keeping the buffer alive.
     *
     * Same rationale as asyncBroadcastMessageByOwnedPayload: the production FrontService dispatches
     * the gateway send off the caller thread (PBFT under m_mutex must not contend the gateway
     * session lock; sendViewChange / sendRecoverResponse run there). Point-to-point sends encode
     * the wire frame anyway, so this is not zero-copy; owning the payload only keeps it alive
     * across the deferred encode. The default implementation bridges to the coroutine
     * sendMessageByNodeID (fire-and-forget).
     *
     * @param moduleID: moduleID
     * @param nodeID: the receiver nodeID
     * @param payload: already-encoded message body; ownership is transferred to the callee
     */
    virtual void asyncSendMessageByNodeIDByOwnedPayload(
        int moduleID, bcos::crypto::NodeIDPtr nodeID, bytesPointer payload)
    {
        task::wait([](FrontServiceInterface::Ptr self, int _moduleID,
                       bcos::crypto::NodeIDPtr _nodeID,
                       bytesPointer _payload) -> task::Task<void> {
            co_await self->sendMessageByNodeID(_moduleID, std::move(_nodeID),
                ::ranges::views::single(bcos::ref(*_payload)), 0);
        }(shared_from_this(), moduleID, std::move(nodeID), std::move(payload)));
    }

    /**
     * @brief: get local protocol info
     * @return bcos::protocol::ProtocolInfo::ConstPtr
     */
    // virtual bcos::protocol::ProtocolInfo::ConstPtr getLocalProtocolInfo() const = 0;
};

}  // namespace bcos::front
