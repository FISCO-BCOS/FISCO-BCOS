/*
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file OneShot.h
 * @brief One-shot, allocation-free value delivery to a coroutine: the asynchronous counterpart of
 *        Immediate.
 */

#pragma once

#include "bcos-task/Task.h"
#include <exception>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace bcos::task
{
// A ONE-SHOT slot carrying Values... to a coroutine that may `co_await` it: the value arrives LATER
// (possibly never), exactly once — the asynchronous counterpart of Immediate, whose value is
// already there and which never suspends.
//
// How it relates to FireAwaitable, the library's other asynchronous awaitable: FireAwaitable
// adapts ONE callback-style initiation (an initiate callable plus its asio-style completion
// handler) into a co_await, rescuing the wait with an initial error when the handler is dropped
// uninvoked. OneShot is instead a NAMED slot that several independent producers can race to
// settle (ack vs timeout vs teardown flush), with an explicit cancel() teardown path, and with
// consumer exceptions propagating to the settler rather than being logged and swallowed. Neither
// replaces the other: wrap a callback API with makeFireAwaitable; use OneShot when the completion
// comes from an event that is not the operation's own caller. (Dedicated wrappers cover their own
// specific idioms — e.g. Deadline.h's awaitableSleep for the hand-built timer awaitable.)
//
// This is the pattern that kept being hand-rolled wherever an operation is completed by an event
// that is not its own caller — a write completion fired by a write loop, an acknowledgement fired
// by a receive pump, a timeout fired by a timer:
//
//     task::Task<...> operation(...)
//     {
//         auto once = std::make_shared<OneShot<Error>>();
//         registerWithCompletionSink([once](Error e) { once->complete(e); });  // any thread
//         auto [error] = co_await *once;                                      // suspends
//         ...
//     }
//
// Semantics, and why each one is load-bearing:
//  - EXACTLY-ONCE: the first complete() wins; later and concurrent calls are no-ops. This is what
//    lets several independent producers race to settle one wait (ack vs timeout vs teardown flush)
//    without a hand-written claim/cancel protocol.
//  - NO LOST WAKEUP: a completion that arrives before the coroutine parks is delivered on the fast
//    path (await_ready), so the ordering "complete() then co_await" is safe.
//  - TOTAL COMPLETION: every path that can end the wait must settle the slot — complete() with a
//    value, or cancel() when there is no value to deliver (socket torn down, host stopped, request
//    dropped). A cancelled wait throws in await_resume() instead of returning default-constructed
//    values, so a teardown can never be mistaken for a real result. cancel() exists as its own
//    name — not just as an error-flavoured complete() — because the tree has repeatedly
//    hand-rolled that wake-up.
//  - NO ALLOCATION: the single mutable slot lives inside this object (hence inside the coroutine
//    frame when declared as a local), not on the heap. Heap-own it only when the completion sink
//    outlives the frame — every path that can settle it must then go through the shared_ptr, which
//    is exactly the ownership the old std::function-callback version left implicit.
//
// LIFETIME CONTRACT: the OneShot must outlive every co_await on it and every settle call —
// complete()/cancel() touch the slot itself. The waiting frame, by contrast, may be destroyed
// at any time: destroying it while parked ABANDONS the wait (the parked awaitable deregisters
// itself, so a later settle only records the outcome instead of resuming a dead frame). That is
// what makes the heap-owned teardown order "drop the consumer, then flush the sink" safe. The
// one ordering a caller must still serialize is destroying the parked frame against an IN-FLIGHT
// settle: a settle that already claimed the resume owns the frame until its resume() returns.
//
// Threading: complete() and cancel() may be called from any thread; the awaiting coroutine is
// resumed on whichever thread settled it. A caller that needs the continuation to run on a specific
// executor must post the settling call there (this is what Session::postCallback did by hand).
template <typename... Values>
class [[nodiscard]] OneShot
{
public:
    using Ptr = std::shared_ptr<OneShot>;

    OneShot() = default;
    OneShot(const OneShot&) = delete;
    OneShot(OneShot&&) = delete;
    OneShot& operator=(const OneShot&) = delete;
    OneShot& operator=(OneShot&&) = delete;
    ~OneShot() = default;

    // Settle the slot with a value. Returns false when it had already been settled (by a peer
    // producer, by an earlier completion, or by cancel()) — the caller may use that to tell "I was
    // the one who settled it" from "somebody beat me to it".
    bool complete(Values... values)
    {
        if (!claim())
        {
            return false;
        }
        m_result.data = std::make_tuple(std::move(values)...);
        publish();
        return true;
    }

    // Settle the slot WITHOUT a value: the wait ends in failure. Idempotent, thread-safe, and the
    // counterpart of complete() on every teardown path — a host shutdown, a dropped request, a
    // session closed under a parked sender. A parked (or later) awaiter rethrows `error` from
    // await_resume(); with no error given it throws std::runtime_error("one-shot slot cancelled").
    // If the parked awaiter does not catch that rethrow, it propagates back out of cancel() to
    // the caller (see publish() — this is why the settle path is not noexcept).
    // Calling it after complete() is a no-op, so a value that did arrive is never lost.
    bool cancel(std::exception_ptr error = nullptr)
    {
        if (!claim())
        {
            return false;
        }
        m_result.error = error ?
                             std::move(error) :
                             std::make_exception_ptr(std::runtime_error("one-shot slot cancelled"));
        publish();
        return true;
    }

    // Whether a producer has settled it, by either complete() or cancel(). Only says that the wait
    // is over; await_resume() is what reports whether a value or a cancellation ended it.
    bool settled() const noexcept { return m_result.completed.load(std::memory_order_acquire); }

    // Await the slot. `co_await once` and `co_await *oncePtr` both work. The returned
    // awaitable holds a reference into this object, which must outlive the co_await.
    GetResultAwaitable<Values...> operator co_await()
    {
        return GetResultAwaitable<Values...>(m_result);
    }

private:
    // The shared state is task::GetResultAwaitable's Result: the exactly-once claim, the suspend
    // handshake (INIT/SUSPENDED/WOKEN/DONE) and the fast path are all implemented there and are
    // shared with the frame-embedded users of that primitive. This class only adds ownership
    // ergonomics and the cancel path.
    using Result = GetResultAwaitable<Values...>::Result;

    // The exactly-once claim, shared with GetResultAwaitable::complete() so that a cancel() racing
    // a complete() settles the wait exactly once.
    bool claim() noexcept
    {
        auto expected = false;
        return m_result.completed.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel, std::memory_order_acquire);
    }

    // Publish the settled state: resume the parked awaiter, or record the settle when nobody is
    // parked (never parked — a later co_await takes the fast path — or the wait was abandoned).
    // Shares GetResultAwaitable's publish handshake so both settle paths are interchangeable —
    // including the exception contract: resume() runs the awaiter INLINE, so an awaiter that does
    // not catch (e.g. a task::wait consumer whose await_resume() rethrows a cancellation) lets the
    // exception propagate back out of publish() to the caller of complete()/cancel(). Like
    // complete(), this is therefore deliberately NOT noexcept — marking it noexcept would turn an
    // uncaught consumer exception into std::terminate on the settling thread.
    void publish() { GetResultAwaitable<Values...>::publish(m_result); }

    Result m_result;
};
}  // namespace bcos::task
