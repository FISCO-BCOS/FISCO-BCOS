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
 * @brief shared types of the front service module: callback typedefs, the module-level send
 *        result and the callback->coroutine bridge. Used by the in-process FrontService
 *        (bcos-front), the tars FrontServiceClient (bcos-tars-protocol) and test fakes.
 * @file FrontServiceTypeDef.h
 */
#pragma once
#include "bcos-crypto/interfaces/crypto/KeyInterface.h"
#include "bcos-framework/gateway/GroupNodeInfo.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/Error.h"
#include <atomic>
#include <coroutine>
#include <functional>

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

}  // namespace bcos::front
