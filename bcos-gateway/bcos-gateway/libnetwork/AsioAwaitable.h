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
 * @brief Bridge the callback-style ASIOInterface operations into task::Task coroutines
 * @file AsioAwaitable.h
 */
#pragma once
#include "bcos-gateway/libnetwork/Common.h"
#include <boost/asio/error.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/system/error_code.hpp>
#include <atomic>
#include <coroutine>
#include <cstdint>
#include <exception>
#include <tuple>
#include <type_traits>
#include <utility>

namespace bcos::gateway
{
// Awaitable bridging one callback-style asio operation into a task::Task coroutine.
//
// Threading contract: initiations are expected to defer — the completion handler is handed to a
// deferred executor (asio, or a post to some io_context) — so the suspended coroutine is resumed
// on the io_context thread that completes the operation, the same thread the old hand-written
// callback chain ran on. A synchronous invocation or drop from the initiating call does not
// corrupt the running coroutine either: it is neutralized by the arm/cancel handshake below.
// resume() is wrapped in try/catch (including catch(...)): an exception escaping the resumed
// coroutine must not unwind the asio handler into io_context::run() (the same containment the
// hand-written callbacks applied). The loop coroutines additionally catch everything internally,
// so this is the second line of defence.
//
// Lifetime contract: the awaitable object lives inside the coroutine frame, and the coroutine
// can only unwind through the completion handler (or the cancellation path below), so the
// `this` capture is valid for as long as the handler may run.
//
// Completion-or-cancel contract: asio normally invokes the completion handler exactly once, but
// only while the io_context keeps running — a handler destroyed WITHOUT being invoked (an
// io_context torn down with the operation still pending, or a test fake that drops the handler)
// would otherwise pin the suspended coroutine frame forever, leaking everything the frame owns
// (the strong Session/Host reference, socket, buffers). The completion therefore watches its
// own destruction: an armed completion that is destroyed without having run DESTROYS the
// suspended frame, releasing its references. Destroying — rather than resuming — the coroutine
// runs no loop code: at that point the objects the loop body would touch may already be gone or
// deliberately torn down, and this matches the base semantics, where destroying an uninvoked
// handler simply released its captured shared_ptr without firing any logic.
//
// Arm/cancel handshake: the completion and await_suspend share ONE atomic state (see
// detail::CompletionState below), and each side claims its transition with a single
// compare_exchange_strong, so a completion that resolves DURING initiation can never touch a
// frame that has not suspended yet, and exactly one side wins the transition:
//  - await_suspend claims Init -> Suspended only after initiate() returns. On success the
//    coroutine suspends and the completion owns the resume; on failure the observed state
//    (Completed or Dropped) means the completion already settled synchronously inside
//    initiate(), so await_suspend resumes inline (returns false) instead.
//  - operator() writes the result, then claims Init -> Completed. On success await_suspend will
//    observe it and resume inline — operator() must NOT resume the still-running frame; on
//    failure the state is Suspended, so it resumes the suspended coroutine.
//  - ~AsioCompletion claims Init -> Dropped when an armed completion is destroyed without
//    invocation. On success await_suspend observes it and resumes inline with operation_aborted;
//    on failure the state is Suspended, so the frame-destroying rescue runs.
// The single-atomic claim removes the check-then-set window a multi-flag handshake has: a
// cross-thread completion landing between a read and a later store can no longer be lost.
//
// Rescue scope: destroying the suspended frame releases the strong Session/Host reference, the
// socket and the buffers — everything the coroutine body owns. One small frame is NOT
// reclaimed: task::wait wraps these loop coroutines in an AsyncTask frame that is resumed only
// through the inner coroutine's final_suspend, so destroying the inner handle directly orphans
// the wrapper frame (a bounded, shutdown-scoped leak — one small AsyncTask per rescued
// operation).
namespace detail
{
// Handshake state shared by the completion and await_suspend (see the arm/cancel handshake
// contract above). Each transition is claimed by exactly one side via compare_exchange_strong.
enum class CompletionState : uint8_t
{
    Init = 0,   // no side has claimed the completion yet
    Suspended,  // await_suspend has suspended the coroutine; the completion owns the resume
    Completed,  // operator() ran (result written); await_suspend resumes inline
    Dropped     // the armed completion was destroyed uninvoked; await_suspend fails inline
};
// Result tuple for the dropped-completion path: the first element (always boost::system::
// error_code for every awaitable in this file) is operation_aborted, the remaining elements are
// zero-initialized so the awaiting coroutine sees a well-formed error result.
template <typename First, typename... Rest>
std::tuple<First, Rest...> makeOperationAbortedResult()
{
    return std::make_tuple(
        boost::system::error_code(boost::asio::error::operation_aborted), Rest{}...);
}

// The completion handler handed to asio. Move-only: asio moves (never copies) the handler into
// the operation, and a move transfers the completion duty to the new instance. Exactly one of
// three exits happens: operator() runs the handler path, the armed instance is destroyed while
// the coroutine is suspended (the rescue above), or the armed instance is destroyed while the
// coroutine is still running on the initiator's stack (a synchronous drop inside initiate()),
// which is recorded as a cancellation for await_suspend to observe (see the handshake above).
template <typename... Results>
class AsioCompletion
{
public:
    AsioCompletion(std::tuple<Results...>* result, std::coroutine_handle<> handle,
        std::atomic<CompletionState>* state)
      : m_result(result), m_handle(handle), m_state(state)
    {}
    AsioCompletion(AsioCompletion&& other) noexcept
      : m_result(other.m_result), m_handle(other.m_handle), m_state(other.m_state),
        m_armed(other.m_armed)
    {
        // the moved-from instance no longer owns the completion duty
        other.m_armed = false;
    }
    AsioCompletion(const AsioCompletion&) = delete;
    AsioCompletion& operator=(const AsioCompletion&) = delete;
    AsioCompletion& operator=(AsioCompletion&&) = delete;

    ~AsioCompletion() noexcept
    {
        try
        {
            if (!m_armed)
            {
                // moved-from, or the completion already ran: nothing to settle
                return;
            }
            // The Init -> Dropped claim is safe in EVERY destruction context, including stack
            // unwinding: it only records the cancellation — it never touches the frame. When the
            // destruction comes from an initiation failure unwinding through await_suspend, the
            // claim succeeds harmlessly and await_suspend's catch resumes inline and rethrows
            // the initiation error (the Dropped state is never consulted); any other Init-state
            // destruction (a test fake dropping the handler, io_context teardown on a thread
            // that happens to be unwinding) records Dropped so await_suspend resumes inline with
            // operation_aborted instead of parking forever. This is why the claim must NOT be
            // gated on std::uncaught_exceptions(): that thread-global counter keys on an
            // unrelated property (is THIS thread unwinding right now), not on the fact that
            // matters (did initiate() throw on this stack), and gating on it would strand a
            // genuinely armed completion destroyed on an unwinding thread. The frame-destroying
            // rescue below needs no such guard either: Suspended is published by await_suspend's
            // CAS only after initiate() returned normally, so a Suspended frame is structurally
            // never the still-running case.
            auto expected = CompletionState::Init;
            if (m_state->compare_exchange_strong(
                    expected, CompletionState::Dropped, std::memory_order_acq_rel))
            {
                // we claimed the drop: await_suspend has not suspended yet (state was Init),
                // so it will observe Dropped and resume inline with operation_aborted
                return;
            }
            if (expected == CompletionState::Suspended ||
                m_state->load(std::memory_order_acquire) == CompletionState::Suspended)
            {
                // the coroutine has suspended and no completion will ever come: release the
                // frame (and everything it owns) — the completion-or-cancel rescue
                m_handle.destroy();
            }
        }
        catch (...)
        {}
    }

    void operator()(Results... results)
    {
        if (!m_armed)
        {
            // asio guarantees exactly one invocation; this also rejects a moved-from instance
            return;
        }
        m_armed = false;
        *m_result = std::make_tuple(std::move(results)...);
        auto expected = CompletionState::Init;
        if (m_state->compare_exchange_strong(
                expected, CompletionState::Completed, std::memory_order_acq_rel))
        {
            // we claimed the completion: await_suspend has not suspended yet, so it will
            // observe Completed and resume inline (return false) — resuming here would resume
            // a still-running frame (UB)
            return;
        }
        if (expected == CompletionState::Suspended)
        {
            // normal asynchronous completion: the coroutine has suspended, resume it
            resume();
        }
    }

private:
    void resume() noexcept
    {
        try
        {
            m_handle.resume();
        }
        catch (std::exception const& e)
        {
            ASIO_LOG(WARNING) << LOG_DESC("asio awaitable resume exception")
                              << LOG_KV("what", boost::diagnostic_information(e));
        }
        catch (...)
        {
            ASIO_LOG(WARNING) << LOG_DESC("asio awaitable resume exception")
                              << LOG_KV("what", boost::current_exception_diagnostic_information());
        }
    }

    std::tuple<Results...>* m_result;
    std::coroutine_handle<> m_handle;
    std::atomic<CompletionState>* m_state;
    bool m_armed = true;
};
}  // namespace detail

template <typename Initiate, typename... Results>
struct AsioAwaitable
{
    Initiate m_initiate;
    std::tuple<Results...> m_result{};
    std::exception_ptr m_error = nullptr;
    // Arm/cancel handshake with the completion (see the detail::CompletionState contract above):
    // a single atomic state, claimed once by whichever side wins — await_suspend (Suspended),
    // the completion (Completed), or its destructor (Dropped). A cross-thread completion landing
    // between a read and a store of a multi-flag handshake can no longer be lost.
    std::atomic<detail::CompletionState> m_state{detail::CompletionState::Init};

    constexpr bool await_ready() const noexcept { return false; }
    bool await_suspend(std::coroutine_handle<> handle)
    {
        // Move the initiate function onto the stack: the completion it schedules may resume (and
        // finish) this coroutine on another thread before initiate() returns — for the write
        // path the initiating thread is an arbitrary producer while completion runs on the
        // socket's io_context — destroying this frame while initiate() is still on the stack.
        auto initiate = std::move(m_initiate);
        try
        {
            // The completion is moved into the operation: initiate must either hand it to a
            // deferred executor (asio) or invoke it before returning — never drop it. Both the
            // synchronous-invocation and the synchronous-drop cases are neutralized by the
            // handshake (the completion's CAS observes Init and defers to await_suspend instead
            // of touching the running frame); the only sanctioned drop is initiation failure,
            // which unwinds through this catch.
            initiate(detail::AsioCompletion<Results...>(&m_result, handle, &m_state));
        }
        catch (...)
        {
            // Initiation itself threw: no handler reached asio, so nothing will resume the
            // coroutine. Resume immediately by NOT suspending; await_resume rethrows so the
            // loop's own catch handles it, exactly as a synchronous throw from the old
            // callback-style initiation did.
            m_error = std::current_exception();
            return false;
        }
        // After initiate() returns, touch only the single atomic state below — and only until
        // the CAS decides: on success we return immediately (the completion owns the resume), so
        // a cross-thread resume cannot catch us touching members; on failure the completion
        // settled synchronously inside initiate() WITHOUT resuming (it observed Init and
        // deferred to us), so the frame is still running and safe to read.
        auto expected = detail::CompletionState::Init;
        if (m_state.compare_exchange_strong(
                expected, detail::CompletionState::Suspended, std::memory_order_acq_rel))
        {
            // we claimed the suspend: the coroutine is now parked and the completion (or its
            // destructor) will resume / release it
            return true;
        }
        if (expected == detail::CompletionState::Completed)
        {
            // the completion fired synchronously inside initiate(): the result is already in
            // m_result, so resume inline (return false) instead of suspending
            return false;
        }
        // Dropped: the completion was dropped synchronously inside initiate() without throwing.
        // Resume inline with operation_aborted so the awaiting coroutine completes with an error
        // instead of suspending forever.
        m_result = detail::makeOperationAbortedResult<Results...>();
        return false;
    }
    std::tuple<Results...> await_resume()
    {
        if (m_error)
        {
            std::rethrow_exception(m_error);
        }
        return std::move(m_result);
    }
};

template <typename... Results, typename Initiate>
auto makeAsioAwaitable(Initiate&& initiate)
{
    return AsioAwaitable<std::decay_t<Initiate>, Results...>{std::forward<Initiate>(initiate), {}};
}
}  // namespace bcos::gateway
