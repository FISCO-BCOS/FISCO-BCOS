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
 * @file Deadline.h
 * @brief Coroutine-facing timer: the Deadline guard that CANCELS a pending operation on expiry.
 */

#pragma once

#include "bcos-task/Task.h"
#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <atomic>
#include <chrono>
#include <coroutine>
#include <cstdint>
#include <memory>
#include <utility>

namespace bcos::task
{
namespace detail
{
// Self-contained sleep awaitable: the timer lives inside the awaitable, i.e. inside the awaiting
// coroutine's frame, so it stays alive across the suspension and is cancelled+destroyed for free
// when the frame is resumed or destroyed.
struct SleepAwaitable
{
    boost::asio::steady_timer m_timer;
    boost::system::error_code m_error;

    SleepAwaitable(boost::asio::io_context& ioService, uint32_t durationMs)
      : m_timer(ioService, std::chrono::milliseconds(durationMs))
    {}

    static constexpr bool await_ready() noexcept { return false; }

    // The handler writes the error into this frame-owned member and resumes; the timer may then be
    // destroyed without cancelling an in-flight wait.
    void await_suspend(std::coroutine_handle<> handle)
    {
        m_timer.async_wait(
            [this, handle](const boost::system::error_code& error)
            {
                m_error = error;
                handle.resume();
            });
    }

    boost::system::error_code await_resume() const noexcept { return m_error; }
};
}  // namespace detail

// Suspend the calling coroutine for `durationMs` on `ioContext`.
//
// The wait completes with boost::system::error_code{} on expiry or
// boost::asio::error::operation_aborted when the timer was cancelled. The timer is owned by the
// awaiting coroutine's frame, so a caller that returns early simply leaves it to be destroyed
// (which cancels it) — no cancel/re-arm bookkeeping.
//
// This replaces the hand-built `task::makeFireAwaitable<error_code>([&timer](auto handler) {
// timer.async_wait(std::move(handler)); }, operation_aborted)` idiom, which every backoff /
// heartbeat-style loop in the tree was spelling out separately.
inline task::Task<boost::system::error_code> awaitableSleep(
    boost::asio::io_context& ioContext, uint32_t durationMs)
{
    co_return co_await detail::SleepAwaitable(ioContext, durationMs);
}

// ---------------------------------------------------------------------------------------------
// Deadline: one timer that CANCELS a pending operation when it expires, with the late-expiry race
// handled.
//
// The race is the whole reason this is a type instead of two lines: a timer's expiry handler may
// already be QUEUED on the io thread when the operation completes successfully, and cancel()
// cannot recall it. A queued expiry that then ran unchecked would cancel the NEXT operation on the
// same socket. A per-arm generation counter closes that window: every arm() and every disarm()
// bumps its own generation, and an expiry only fires when its arm's generation is still current. A
// stale expiry therefore does nothing, whichever thread it runs on and whichever thread re-armed
// the deadline.
//
// THREADING CONTRACT: the timer is serviced on `ioService()`, so `timeoutHandler` and the timer's
// own completion run there; arm()/disarm() are expected from the same executor but are correct from
// any thread — generation, armed and expired are atomic, and cancel() is safe to call from any
// thread. Atomicity is load-bearing rather than defensive precisely because a caller may resume on
// a different context than the socket's: Host::clientConnect, for example, resumes on the
// resolver's context when resolveConnect reports a resolve failure.
class Deadline
{
public:
    Deadline(boost::asio::io_context& ioService, uint32_t timeoutMs)
      : m_state(std::make_shared<State>(ioService, timeoutMs))
    {}

    Deadline(const Deadline&) = delete;
    Deadline(Deadline&&) = delete;
    Deadline& operator=(const Deadline&) = delete;
    Deadline& operator=(Deadline&&) = delete;

    // Destroying an armed Deadline cancels the timer; a later expiry sees a stale generation and
    // no-ops. Note this only CANCELS; it does not release the owner's reference to the state. A
    // queued expiry still holds a reference, so the timer outlives this owner for at most one
    // handler turn — which matters when the handler captured a socket (see arm()).
    ~Deadline()
    {
        if (m_state->armed.load(std::memory_order_acquire))
        {
            // Bump the generation BEFORE cancelling, so a handler that is already queued cannot
            // fire on a re-armed deadline (see the note on arm()).
            (void)m_state->generation.fetch_add(1, std::memory_order_acq_rel);
            m_state->armed.store(false, std::memory_order_release);
            // basic_waitable_timer::cancel() has no error_code overload; it never throws.
            (void)m_state->timer.cancel();
        }
    }

    // Arm the deadline. `timeoutHandler` runs on expiry, on the timer's io_context, with an empty
    // error code: it is where "close the socket" / "cancel the read" lives, so a socket reference
    // captured there is released as soon as the handler is destroyed, without coupling this type to
    // a socket type.
    //
    // Takes the callable by value as a TEMPLATE parameter, not a std::function: the handler is
    // handed straight to asio, so a lambda stays a concrete closure in the timer's handler. A
    // std::function would erase the type and — for the common capture set (a socket/closing object
    // plus the enclosing this) — exceed libstdc++'s 16-byte small-object buffer, costing a heap
    // allocation per arm. On the session read loop that is once per timeout-configured read.
    template <class TimeoutHandler>
    void arm(TimeoutHandler timeoutHandler)
    {
        auto state = m_state;
        // A fresh generation per arm is the entire late-expiry guard. arm() bumps it BEFORE touching
        // the timer, so the bump lands ahead of any handler that basic_waitable_timer hands back
        // synchronously while the expiry changes. The handler compares against the generation it was
        // armed with, so anything queued by an earlier arm — a natural expiry, one that lost the race
        // against a successful operation, or asio's own submit-on-re-arm — is rejected.
        const auto armedGeneration = state->generation.fetch_add(1, std::memory_order_acq_rel) + 1;
        state->armed.store(true, std::memory_order_release);
        state->expired.store(false, std::memory_order_release);

        // Move the deadline to "now + timeoutMs" for THIS arm. Without this every later arm would
        // inherit the first arm's expiry and, being already in the past, fire (and cancel its own
        // operation) immediately. The old code got away with it only because it was constructed per
        // operation; it is asserted once and re-armed on the session read loop.
        state->timer.expires_after(state->timeout);
        state->timer.async_wait(
            [state, armedGeneration, timeoutHandler = std::move(timeoutHandler)](
                const boost::system::error_code& error) mutable {
                // Release this handler's reference on EVERY exit, so the timer's lifetime stays
                // tied to the Deadline (and to the next arming) instead of being held for a full
                // timeout period after teardown. Done FIRST on the expiry path so that re-arming a
                // long timeout inside the handler cannot pin the previous state via this frame.
                auto releaseState = std::move(state);
                // `error` is not a reliable "this arm was cancelled" test: asio may report success
                // for a handler it submitted because the expiry CHANGED. The generation is the
                // authoritative check; armed is a cheap second gate for the disarmed case.
                if (error || !releaseState->armed.load(std::memory_order_acquire) ||
                    releaseState->generation.load(std::memory_order_acquire) != armedGeneration)
                {
                    // Cancelled by disarm()/~Deadline, superseded by a later arm(), or an expiry
                    // that was already queued when the deadline was disarmed and/or re-armed. (A
                    // re-arm inside the handler is safe: asio allows a timer to be re-armed while
                    // its handler runs.)
                    return;
                }
                releaseState->expired.store(true, std::memory_order_release);
                timeoutHandler(boost::system::error_code{});
            });
    }

    // Disarm, and invalidate any expiry that is already queued. Safe to call when never armed, from
    // any thread. Unlike the old armed-flag protocol this does NOT require the caller to still hold
    // the completed operation's result: the generation bump alone refuses the stale expiry, so a
    // queued expiry can never cancel the next operation on the same socket.
    void disarm()
    {
        (void)m_state->generation.fetch_add(1, std::memory_order_acq_rel);
        m_state->armed.store(false, std::memory_order_release);
        // basic_waitable_timer::cancel() has no error_code overload; it never throws.
        (void)m_state->timer.cancel();
    }

    // Whether the deadline actually fired for the operation currently being settled. Only
    // meaningful after arm() and before the next arm().
    bool expired() const noexcept { return m_state->expired.load(std::memory_order_acquire); }

private:
    // ONE allocation for the timer, the flags and the generation counter. The handler captures this
    // same object, so its destructor (which destroys the timer) is what finally reclaims the
    // allocation once the last queued expiry has run.
    struct State
    {
        State(boost::asio::io_context& ioService, uint32_t timeoutMs)
          : timer(ioService, std::chrono::milliseconds(timeoutMs)), timeout(timeoutMs)
        {}

        boost::asio::steady_timer timer;
        // Re-applied by every arm(): the deadline moves with each operation, it is not fixed at
        // construction (the session read loop arms the same Deadline once per read).
        const std::chrono::milliseconds timeout;
        std::atomic_bool armed{false};
        std::atomic_bool expired{false};
        // Bumped by every arm() and every disarm(). An expiry handler only fires when its own arm's
        // generation is still current, which is what makes a queued (unrecallable) expiry harmless.
        std::atomic<uint64_t> generation{0};
    };

    std::shared_ptr<State> m_state;
};
}  // namespace bcos::task
