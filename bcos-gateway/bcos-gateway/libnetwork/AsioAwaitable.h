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
#include <bcos-task/Task.h>
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
    AsioCompletion(std::tuple<Results...>* result, std::coroutine_handle<> handle) : m_result(result), m_handle(handle) {}
    AsioCompletion(AsioCompletion&& other) noexcept
      : m_result(other.m_result), m_handle(other.m_handle), m_armed(other.m_armed)
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
            m_handle.resume();
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

private:
    std::tuple<Results...>* m_result;
    std::coroutine_handle<> m_handle;
    bool m_armed = true;
};
}  // namespace detail

template <typename Initiate, typename... Results>
struct AsioAwaitable
{   
    Initiate m_initiate;
    std::tuple<Results...> m_result;
    std::exception_ptr m_error = nullptr;

    constexpr bool await_ready() const noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle)
    {
        try
        {
            // create a coroutine task to invoke the initiate function
            // create a completion handler and pass it to the initiate as callback function
            auto task = [this](auto completion) mutable -> task::TaskPure {
                try
                {
                    m_initiate(std::move(completion));
                }
                catch (...)
                {
                    m_error = std::current_exception();
                }
            }(detail::AsioCompletion<Results...>(&m_result, handle));
            return task.getHandle();
        }
        catch (...)
        {
            m_error = std::current_exception();
            return handle;
        }
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
}  // namespace bcos::gateway
