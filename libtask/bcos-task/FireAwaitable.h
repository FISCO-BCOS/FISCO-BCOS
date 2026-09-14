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
 * @brief Bridge a callback-style async initiation into a Task coroutine via symmetric transfer.
 * @file FireAwaitable.h
 */
#pragma once
#include "bcos-task/Task.h"
#include <atomic>
#include <coroutine>
#include <cstdio>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

namespace bcos::task
{
// Bridge a callback-style async initiation into a Task coroutine via symmetric transfer.
//
// await_suspend builds a fire-and-forget TaskPure "bridge" coroutine that owns the actual
// initiation, then returns the bridge handle. Symmetric transfer guarantees the awaiting
// coroutine is suspended BEFORE the bridge runs, so when the bridge calls initiate the awaiting
// coroutine is already suspended — the completion may then resume it unconditionally, removing
// the "resume a not-yet-suspended frame" hazard an inline initiate has.
//
// The result is a std::tuple<Error, Results...>: the Error slot is always first. m_result is
// initialized with (failureError, default-constructed Results...), so a completion destroyed
// without being invoked (rescue: executor torn down / initiate dropped the handler) or a failed
// bridge-frame construction simply resumes the awaiting coroutine with that initial error. The
// async operation invokes the completion as (error, results...), which is stored verbatim — a
// non-empty error means failure, the default-constructed Error means success.

namespace detail
{
// Emit the rescued resume exception to stderr so an initiate failure is distinguishable from an
// ordinary teardown in the log. libtask cannot use BCOS_LOG without a bcos-task -> bcos-utilities
// link dependency, so a bare stderr write keeps these silent catches diagnosable without one.
inline void logResumeException(const char* context) noexcept
{
    try
    {
        std::fprintf(stderr, "[FireAwaitable] %s: %s\n", context,
            boost::current_exception_diagnostic_information().c_str());
    }
    catch (...)
    {
        // the diagnostic itself failed; nothing more to do
    }
}

// Move-only completion handed to the initiate callable. Created disarmed; the bridge body arms
// it via active() right before initiating, so a completion destroyed before initiation (e.g.
// bridge-frame allocation failure) is a no-op and can never resume a frame await_suspend has not
// finished suspending. On destruction while still armed, it resumes the awaiting coroutine,
// which reads the initial error result — no explicit error write is needed.
template <typename Error, typename... Results>
class FireCompletion
{
public:
    FireCompletion(std::tuple<Error, Results...>* result, std::coroutine_handle<> handle)
      : m_result(result), m_handle(handle)
    {}

    FireCompletion(FireCompletion&& other) noexcept
      : m_result(other.m_result), m_handle(other.m_handle), m_armed(other.m_armed.load())
    {
        // the moved-from instance no longer owns the completion duty
        other.m_armed.store(false);
    }
    FireCompletion(const FireCompletion&) = delete;
    FireCompletion& operator=(const FireCompletion&) = delete;
    FireCompletion& operator=(FireCompletion&&) = delete;

    ~FireCompletion() noexcept
    {
        // Atomically claim the completion: an exchange wins exactly once, so a concurrent
        // operator() and destructor can never both resume the awaiting coroutine.
        if (!m_armed.exchange(false))
        {
            // moved-from, already ran, or never activated (bridge-frame construction failed)
            return;
        }
        // Rescue: armed but never invoked. m_result still holds the initial error, so just resume
        // the awaiting coroutine to let it unwind through its own error path.
        try
        {
            m_handle.resume();
        }
        catch (...)
        {
            logResumeException("resume rescued the awaiting coroutine and it threw");
        }
    }

    // Arm the completion: called by the bridge body right before initiating.
    void active() { m_armed = true; }

    // Invoked by the async operation with (error, results...): the error comes first, matching
    // asio's completion-handler signature (error_code, size_t, ...). The delivered values are
    // stored verbatim — success or failure is expressed by the error value itself, not by an
    // implicit empty Error here.
    void operator()(Error error, Results... results)
    {
        // Atomically disarm: wins exactly once, so the completion is settled once even under a
        // concurrent destructor.
        if (!m_armed.exchange(false))
        {
            return;
        }
        *m_result = std::make_tuple(std::move(error), std::move(results)...);
        try
        {
            m_handle.resume();
        }
        catch (...)
        {
            // an exception escaping the resumed coroutine must not unwind the caller's handler
            logResumeException("resume completed the awaiting coroutine and it threw");
        }
    }

private:
    std::tuple<Error, Results...>* m_result;
    std::coroutine_handle<> m_handle;
    std::atomic<bool> m_armed = false;
};
}  // namespace detail

template <typename Error, typename Initiate, typename... Results>
struct FireAwaitable
{
    Initiate m_initiate;
    // Initialized with (failureError, empty Results...); a successful completion overwrites it
    // with (Error{}, results...).
    std::tuple<Error, Results...> m_result;

    explicit FireAwaitable(Initiate initiate, Error failureError)
      : m_initiate(std::move(initiate)),
        m_result(std::make_tuple(std::move(failureError), Results{}...))
    {}

    constexpr bool await_ready() const noexcept { return false; }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle)
    {
        try
        {
            // Build the bridge coroutine (parked at initial_suspend): it arms the completion and
            // runs initiate. Returning its handle is a symmetric transfer — the compiler suspends
            // the awaiting coroutine first, then resumes the bridge, so the awaiting frame is
            // always suspended when initiate runs.
            auto task = [](auto completion, Initiate initiate) mutable -> TaskPure {
                try
                {
                    completion.active();
                    initiate(std::move(completion));
                }
                catch (...)
                {
                    // initiate threw after arming: the completion's destructor already resumed
                    // the awaiting coroutine (with the initial error). Swallow the exception but
                    // keep its diagnostic so an initiate failure is distinguishable from teardown.
                    detail::logResumeException(
                        "initiate threw; awaiting coroutine settled with the failure error");
                }
                co_return;
            }(detail::FireCompletion<Error, Results...>(&m_result, handle), std::move(m_initiate));
            return task.getHandle();
        }
        catch (...)
        {
            // bridge-frame construction failed (bad_alloc): resume the awaiting coroutine with
            // the initial error instead of suspending forever.
            return handle;
        }
    }

    std::tuple<Error, Results...> await_resume()
    {
        return std::move(m_result);
    }
};

// Factory: Initiate is deduced from the initiate callable; Error and Results are specified
// explicitly. The parameter pack Results cannot be deduced from the arguments, so it must be
// given as the trailing template argument list.
template <typename Error, typename... Results, typename Initiate>
FireAwaitable<Error, std::decay_t<Initiate>, Results...> makeFireAwaitable(
    Initiate&& initiate, Error failureError)
{
    return FireAwaitable<Error, std::decay_t<Initiate>, Results...>{
        std::forward<Initiate>(initiate), std::move(failureError)};
}
}  // namespace bcos::task
