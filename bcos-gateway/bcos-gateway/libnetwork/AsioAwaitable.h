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
#include <boost/system/error_code.hpp>
#include <atomic>
#include <coroutine>
#include <exception>
#include <memory>
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
// (the strong Session/Host reference, socket, buffers). Every asio-side copy of the handler
// shares one AsioCompletion; when the last copy is destroyed without the handler ever having
// run, the completion DESTROYS the suspended frame, releasing its references. Destroying —
// rather than resuming — the coroutine runs no loop code: at this point the objects the loop
// body would touch may already be gone or deliberately torn down, and this matches the base
// semantics, where destroying an uninvoked handler simply released its captured shared_ptr
// without firing any logic.
namespace detail
{
// Completion state shared by every copy asio makes of an operation's handler (asio handlers
// must be copyable for some call sites — e.g. they are type-erased into the std::function of
// ASIOInterface::asyncReadSome — so the single ownership has to live one indirection away).
// Exactly one of two exits happens: complete() runs the handler path, or the last shared owner
// is destroyed first and the destructor destroys the suspended frame. m_completed also
// serialises the two against each other: the destructor can only run once asio has released
// every copy, i.e. after complete() ran on one of them.
template <typename... Results>
class AsioCompletion
{
public:
    AsioCompletion(std::tuple<Results...>* result, std::coroutine_handle<> handle)
      : m_result(result), m_handle(handle)
    {}
    AsioCompletion(const AsioCompletion&) = delete;
    AsioCompletion& operator=(const AsioCompletion&) = delete;

    ~AsioCompletion() noexcept
    {
        try
        {
            if (m_completed.exchange(true))
            {
                return;
            }
            // asio destroyed the handler without invoking it: destroy the suspended frame so
            // its strong references unwind instead of leaking (see the header comment). No
            // result is stored and no loop code runs — resuming here would execute arbitrary
            // loop bodies (drop(), logging) in a context where the objects they touch may
            // already be torn down.
            m_handle.destroy();
        }
        catch (...)
        {}
    }

    void complete(Results... results)
    {
        if (m_completed.exchange(true))
        {
            // asio guarantees exactly one invocation; this is only a defensive guard
            return;
        }
        *m_result = std::make_tuple(std::move(results)...);
        resume();
    }

    // Initiation failed before the handler reached asio: nothing will ever invoke or destroy an
    // asio-side copy, so the destructor must not fire either — the coroutine is still RUNNING on
    // the initiator's stack at that point (we are inside its own await_suspend), and destroying
    // a non-suspended frame is undefined behaviour.
    void disarm() { m_completed.store(true); }

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
                              << LOG_KV(
                                     "what", boost::current_exception_diagnostic_information());
        }
    }

    std::tuple<Results...>* m_result;
    std::coroutine_handle<> m_handle;
    std::atomic<bool> m_completed{false};
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
        auto completion =
            std::make_shared<detail::AsioCompletion<Results...>>(&m_result, handle);
        try
        {
            // copied, not moved: if initiate() throws, the catch below still needs the completion
            initiate([completion](Results... results) mutable {
                completion->complete(std::move(results)...);
            });
        }
        catch (...)
        {
            // Initiation itself threw: no handler reached asio, so nothing will resume the
            // coroutine. Disarm the completion, then resume immediately by NOT suspending;
            // await_resume rethrows so the loop's own catch handles it, exactly as a synchronous
            // throw from the old callback-style initiation did.
            completion->disarm();
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
    return AsioAwaitable<std::decay_t<Initiate>, Results...>{
        std::forward<Initiate>(initiate), {}};
}
}  // namespace bcos::gateway
