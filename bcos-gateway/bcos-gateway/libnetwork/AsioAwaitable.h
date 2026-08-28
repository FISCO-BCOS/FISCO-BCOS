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
#include <boost/exception/diagnostic_information.hpp>
#include <coroutine>
#include <exception>
#include <tuple>
#include <type_traits>
#include <utility>

namespace bcos::gateway
{
// Awaitable bridging one callback-style asio operation into a task::Task coroutine.
//
// Threading contract: the wrapped asio operation never invokes its completion handler inline
// from the initiating call, so the suspended coroutine is always resumed on the io_context
// thread that completes the operation — the same thread the old hand-written callback chain ran
// on. resume() is wrapped in try/catch (including catch(...)): an exception escaping the resumed
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
// handler simply released its captured shared_ptr without firing any logic. The rescue destroys
// only the innermost frame of the co_await chain; production unwinding always comes through
// normal completion (the frame pins the socket, which pins the io_context, so teardown cannot
// destroy a pending handler out from under it), and test fakes must drain parked reads before
// tearing down (see the FakeASIO stopReads() pattern in the unittests).
namespace detail
{
// The completion handler handed to asio. Move-only: asio moves (never copies) the handler into
// the operation, and a move transfers the completion duty to the new instance. Exactly one of
// two exits happens: operator() runs the handler path, or the armed instance is destroyed first
// and the rescue above fires.
template <typename... Results>
class AsioCompletion
{
public:
    AsioCompletion(std::tuple<Results...>* result, std::coroutine_handle<> handle)
      : m_result(result), m_handle(handle)
    {}
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
            // std::uncaught_exceptions(): destruction during stack unwinding means initiation
            // threw (see await_suspend) and the coroutine is still RUNNING on that stack —
            // destroying a non-suspended frame is undefined behaviour, so the rescue stays out
            // of unwinding; await_resume rethrows the initiation error instead.
            if (m_armed && std::uncaught_exceptions() == 0)
            {
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
        resume();
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
    bool m_armed = true;
};
}  // namespace detail

template <typename Initiate, typename... Results>
struct AsioAwaitable
{
    Initiate m_initiate;
    std::tuple<Results...> m_result{};
    std::exception_ptr m_error = nullptr;

    constexpr bool await_ready() const noexcept { return false; }
    bool await_suspend(std::coroutine_handle<> handle)
    {
        // Move the initiate function onto the stack: the completion it schedules may resume (and
        // finish) this coroutine on another thread before initiate() returns — for the write
        // path the initiating thread is an arbitrary producer while completion runs on the
        // socket's io_context — destroying this frame while initiate() is still on the stack.
        // Nothing member-owned may be touched after the call below.
        auto initiate = std::move(m_initiate);
        try
        {
            // The completion is moved into the operation: initiate must either hand it to a
            // deferred executor (asio) or invoke it before returning — never drop it. Dropping
            // an armed completion outside stack unwinding destroys the RUNNING frame (see the
            // destructor's rescue); the only sanctioned drop is initiation failure, which
            // unwinds through this catch.
            initiate(detail::AsioCompletion<Results...>(&m_result, handle));
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
        return true;
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
