/*
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
 */

#pragma once
#include "AsyncTask.h"
#include "Task.h"
#include "Trait.h"
#include <exception>
#include <memory>
#include <type_traits>
#include <variant>

namespace bcos::task
{

constexpr inline struct Wait
{
    static AsyncTask executeTask(auto task) { co_await std::move(task); }

    template <IsAwaitable Task>
    void operator()(Task&& task) const
    {
        auto asyncTask = executeTask(std::forward<Task>(task));
        try
        {
            asyncTask.start();
        }
        catch (...)
        {
            // The exception escaped the detached chain: its top frame is stranded at its final
            // suspend point (see AsyncTask::promise_type::unhandled_exception). Reclaim it —
            // and the frames its parameter copies own — before the caller catches.
            detail::reclaimEscapedFrames();
            throw;
        }
    }
} wait{};

// NOTE (measured 2026-10, do not "optimize" this away): wait()'s AsyncTask wrapper is NOT
// redundant overhead — it is the LIFETIME OWNER of the detached task.
//
// A coroutine frame parked at an inner co_await never runs again on its own, so a
// fire-and-forget task needs an owner that outlives its suspension points. executeTask IS that
// owner: it is the frame that keeps the task alive while it is parked and destroys it when the
// body completes (or, when the body escapes with an exception, has it reclaimed through the
// escaped-frame registry — see AsyncTask.h). Starting the Task and dropping the handle (a
// tempting `task::detach`) frees the frame at the FIRST suspension point instead. Measured,
// with a payload moved into a parked task's frame:
//     task::wait(task)  -> use_count 2 after launch (frame alive, payload owned)
//     task.start()      -> use_count 1 after launch (frame freed, payload destroyed)
// The second form then resumes into freed memory as soon as a completion fires the parked
// operation — which is exactly what it did to FIB184_SessionAsyncLifetimeTest. Keep using wait().

constexpr inline struct SyncWait
{
    template <IsAwaitable Task>
    auto operator()(Task&& task, auto&&... args) const
        -> AwaitableReturnType<std::remove_cvref_t<Task>>
    {
        using ReturnType = AwaitableReturnType<std::remove_cvref_t<Task>>;
        using ReturnTypeWrap = std::conditional_t<std::is_reference_v<ReturnType>,
            std::add_pointer_t<ReturnType>, ReturnType>;
        using ReturnVariant = std::conditional_t<std::is_void_v<ReturnType>,
            std::variant<std::monostate, std::exception_ptr>,
            std::variant<std::monostate, ReturnTypeWrap, std::exception_ptr>>;
        ReturnVariant result;

        auto handle = [](Task&& task, decltype(result)& result,
                          auto&&... args) -> task::SyncTask {
            try
            {
                if constexpr (std::is_void_v<ReturnType>)
                {
                    co_await std::forward<Task>(task);
                }
                else
                {
                    if constexpr (std::is_reference_v<ReturnType>)
                    {
                        decltype(auto) ref = co_await task;
                        result = std::addressof(ref);
                    }
                    else
                    {
                        result.template emplace<ReturnType>(co_await std::forward<Task>(task));
                    }
                }
            }
            catch (...)
            {
                result.template emplace<std::exception_ptr>(std::current_exception());
            }

            // A coroutine body cannot access its own coroutine_handle directly.
            // GetStatusAwaitable uses co_await only to capture the enclosing
            // coroutine's handle in await_suspend and hand back a reference to the
            // promise's m_status. await_suspend returns false, so the coroutine is
            // resumed immediately and this is effectively a non-suspending call.
            // The returned reference is the same object the waiter accesses through
            // SyncTask::getStatus().
            struct GetStatusAwaitable
            {
                std::coroutine_handle<SyncTask::promise_type> m_handle;
                bool await_ready() const noexcept { return false; }
                bool await_suspend(std::coroutine_handle<SyncTask::promise_type> handle) noexcept
                {
                    m_handle = handle;
                    return false;
                }
                std::atomic<WaitStatus>& await_resume() noexcept
                {
                    return m_handle.promise().m_status;
                }
            };

            auto& status = co_await GetStatusAwaitable();

            auto expected = WaitStatus::INIT;
            if (!status.compare_exchange_strong(expected, WaitStatus::FINISHED))
            {
                // CAS failed: the waiter has already set the status to WAITING
                // (slow path). Set FINISHED and wake the waiter up.
                status.store(WaitStatus::FINISHED);
                status.notify_one();
            }
        }(std::forward<Task>(task), result, std::forward<decltype(args)>(args)...);

        auto& status = handle.getStatus();
        handle.start();

        auto expected = WaitStatus::INIT;
        if (status.compare_exchange_strong(expected, WaitStatus::WAITING))
        {
            // CAS succeeded: the coroutine has not finished yet. Wait until it
            // publishes FINISHED.
            status.wait(WaitStatus::WAITING);
        }
        if (auto* exception = std::get_if<std::exception_ptr>(std::addressof(result)))
        {
            std::rethrow_exception(*exception);
        }

        if constexpr (!std::is_void_v<ReturnType>)
        {
            if constexpr (std::is_reference_v<ReturnType>)
            {
                return *(std::get<ReturnTypeWrap>(result));
            }
            else
            {
                return std::move(std::get<ReturnTypeWrap>(result));
            }
        }
    }
} syncWait{};

}  // namespace bcos::task
