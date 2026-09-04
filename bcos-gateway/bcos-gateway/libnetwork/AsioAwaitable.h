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
// Bridge a callback-style asio operation into a task::Task coroutine via symmetric transfer.
//
// await_suspend builds a fire-and-forget TaskPure "bridge" coroutine that owns the actual asio
// initiation, then returns the bridge's handle. Symmetric transfer guarantees the awaiting
// coroutine is suspended BEFORE the bridge runs, so when the bridge calls m_initiate the awaiting
// coroutine is already suspended — the completion may then resume it unconditionally, removing
// the "resume a not-yet-suspended frame" hazard an inline initiate has.
//
// Completion lifecycle (AsioCompletion::m_armed):
//  - Created disarmed; the bridge body arms it via active() right before initiating.
//  - operator() (asio invokes it): writes the result and resumes the awaiting coroutine.
//  - ~AsioCompletion with armed=true (uninvoked: io_context torn down / fake drops the handler):
//    writes operation_aborted and resumes the awaiting coroutine, which unwinds through its own
//    error path — no frame leak and no orphaned wrapper, unlike a raw destroy().
//  - A completion destroyed during bridge-frame construction (e.g. bad_alloc) is still disarmed,
//    so it never resumes a frame that await_suspend has not finished suspending.
//
// resume() is wrapped in try/catch: an exception escaping the resumed coroutine must not unwind
// the asio handler into io_context::run().
//
namespace detail
{
// Result tuple for the dropped-completion path: the first element (always boost::system::
// error_code for every awaitable in this file) is operation_aborted, the remaining elements are
// zero-initialized so the awaiting coroutine sees a well-formed error result.
template <typename First, typename... Rest>
std::tuple<First, Rest...> makeOperationAbortedResult()
{
    static_assert(std::is_same_v<First, boost::system::error_code>,
        "the first result of every asio awaitable must be boost::system::error_code");
    return std::make_tuple(
        boost::system::error_code(boost::asio::error::operation_aborted), Rest{}...);
}
template <typename... Results>
class AsioCompletion
{
public:
    AsioCompletion(std::tuple<Results...>* result, std::coroutine_handle<> handle) : m_result(result), m_handle(handle) {}
    AsioCompletion(AsioCompletion&& other) noexcept
      : m_result(other.m_result), m_handle(other.m_handle), m_armed(other.m_armed.load())
    {
        // the moved-from instance no longer owns the completion duty
        other.m_armed.store(false);
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
                // moved-from, already ran, or never activated (bridge-frame construction failed)
                return;
            }
            // Rescue: an armed completion destroyed without being invoked (io_context torn down,
            // fake drops the handler) reports operation_aborted so the awaiting coroutine unwinds
            // through its own error path instead of being destroyed/leaked.
            *m_result = detail::makeOperationAbortedResult<Results...>();
            m_handle.resume();
        }
        catch (...){}
    }
    // Arm the completion: called by the bridge body right before initiating. The completion is
    // created disarmed so a temporary destroyed during bridge-frame construction (bad_alloc) is a
    // no-op and can never resume a frame await_suspend has not finished suspending.
    void active()
    {
        m_armed = true;
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
    std::atomic<bool> m_armed = false;
};
}  // namespace detail

template <typename Initiate, typename... Results>
struct AsioAwaitable
{   
    Initiate m_initiate;
    std::tuple<Results...> m_result;

    constexpr bool await_ready() const noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle)
    {
        try
        {
            // Build the bridge coroutine (parked at initial_suspend): it arms the completion and
            // runs m_initiate. Returning its handle is a symmetric transfer — the compiler
            // suspends the awaiting coroutine first, then resumes the bridge, so the awaiting
            // frame is always suspended when m_initiate runs.
            auto task = [this](auto completion) mutable -> task::TaskPure {
                try
                {
                    completion.active();
                    m_initiate(std::move(completion));
                }
                catch (...)
                {
                    // initiate threw after arming: the completion's destructor already ran the
                    // rescue (abort + resume). Swallow here to avoid terminate.
                }
                co_return;
            }(detail::AsioCompletion<Results...>(&m_result, handle));
            return task.getHandle();
        }
        catch (...)
        {
            // Bridge-frame construction failed (bad_alloc): returning our own handle resumes the
            // awaiting coroutine immediately (equivalent to not suspending) with an aborted result.
            m_result = detail::makeOperationAbortedResult<Results...>();
            return handle;
        }
    }

    std::tuple<Results...> await_resume()
    {
        return std::move(m_result);
    }
};
template <typename... Results, typename Initiate>
auto makeAsioAwaitable(Initiate&& initiate)
{
    return AsioAwaitable<std::decay_t<Initiate>, Results...>{std::forward<Initiate>(initiate), {}};
}
}  // namespace bcos::gateway
