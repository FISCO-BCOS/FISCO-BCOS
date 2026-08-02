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
#include <atomic>
#include <coroutine>
#include <cstdint>
#include <exception>

namespace bcos::task
{

class [[nodiscard]] AsyncTask
{
public:
    struct promise_type
    {
        static constexpr std::suspend_always initial_suspend() noexcept { return {}; }
        static constexpr auto final_suspend() noexcept
        {
            struct FinalAwaitable
            {
                static constexpr bool await_ready() noexcept { return false; }
                static void await_suspend(std::coroutine_handle<promise_type> handle) noexcept
                {
                    handle.destroy();
                }
                constexpr void await_resume() noexcept {}
            };
            return FinalAwaitable{};
        }
        AsyncTask get_return_object()
        {
            auto handle = std::coroutine_handle<promise_type>::from_promise(
                *static_cast<promise_type*>(this));
            return AsyncTask{handle};
        }
        static void unhandled_exception() { std::rethrow_exception(std::current_exception()); }
        constexpr static void return_void() noexcept {}
    };

    explicit AsyncTask(std::coroutine_handle<promise_type> handle) : m_handle(handle) {}
    AsyncTask(const AsyncTask&) = delete;
    AsyncTask(AsyncTask&& task) noexcept = delete;
    AsyncTask& operator=(const AsyncTask&) = delete;
    AsyncTask& operator=(AsyncTask&& task) = delete;
    ~AsyncTask() noexcept = default;
    void start() { m_handle.resume(); }

private:
    std::coroutine_handle<promise_type> m_handle;
};

enum class WaitStatus : uint8_t
{
    INIT,
    WAITING,
    FINISHED,
};

/*
 * SyncTask ties the lifetime of the synchronization state to the coroutine frame.
 *
 * The synchronization state (m_status) and a reference counter (m_count) live inside
 * the coroutine frame (promise_type), so their lifetime equals the frame lifetime.
 *
 * Lifetime protocol (reference counting):
 *   - m_count starts at 1, owned by the SyncTask handle.
 *   - start() increments it to 2 before resuming the coroutine.
 *   - The coroutine's FinalAwaitable::await_suspend decrements it at final_suspend,
 *     i.e. after the coroutine has executed every byte of its frame (program order)
 *     and will not touch the frame afterwards.
 *   - ~SyncTask() decrements it.
 *   - The side that brings the counter to zero destroys the frame.
 *
 * Why this closes the use-after-destroy races:
 *   - The completing coroutine can safely call notify_one() on m_status: it holds its
 *     own reference until final_suspend, so the frame (and m_status) cannot be
 *     destroyed by the waiter while notify_one() runs.
 *   - The waiter (SyncTask handle) never destroys a frame that the completer may
 *     still be executing in, because destruction requires the completer to have
 *     already decremented the counter at final_suspend.
 *   - If start() is never called the counter stays at 1, so ~SyncTask() destroys the
 *     frame and a never-started task does not leak.
 */
class [[nodiscard]] SyncTask
{
public:
    struct promise_type
    {
        // Reference counter guarding the frame lifetime, see the comment above.
        // Starts at 1 (owned by the SyncTask handle); start() raises it to 2.
        std::atomic<uint8_t> m_count{1};
        // Synchronization state shared between the waiter and the coroutine.
        std::atomic<WaitStatus> m_status{WaitStatus::INIT};

        static constexpr std::suspend_always initial_suspend() noexcept { return {}; }
        static constexpr auto final_suspend() noexcept
        {
            struct FinalAwaitable
            {
                static constexpr bool await_ready() noexcept { return false; }
                static void await_suspend(std::coroutine_handle<promise_type> handle) noexcept
                {
                    if (handle.promise().m_count.fetch_sub(1) == 1)
                    {
                        handle.destroy();
                    }
                }
                constexpr void await_resume() noexcept {}
            };
            return FinalAwaitable{};
        }
        SyncTask get_return_object()
        {
            auto handle = std::coroutine_handle<promise_type>::from_promise(
                *static_cast<promise_type*>(this));
            return SyncTask{handle};
        }
        void unhandled_exception()
        {
            // Abnormal termination: the coroutine body threw an uncaught exception,
            // so control never reaches final_suspend and the reference added by
            // start() would never be released there. Release it here. If this was
            // the last reference, the frame must be destroyed now; otherwise the
            // SyncTask handle's destructor will destroy it. The exception object
            // lives in thread-local storage, so rethrowing after destroy() is safe.
            auto handle = std::coroutine_handle<promise_type>::from_promise(*this);
            if (m_count.fetch_sub(1) == 1)
            {
                handle.destroy();
            }
            std::rethrow_exception(std::current_exception());
        }
        constexpr static void return_void() noexcept {}
    };

    explicit SyncTask(std::coroutine_handle<promise_type> handle) : m_handle(handle) {}
    SyncTask(const SyncTask&) = delete;
    SyncTask(SyncTask&& task) noexcept = delete;
    SyncTask& operator=(const SyncTask&) = delete;
    SyncTask& operator=(SyncTask&& task) = delete;
    ~SyncTask() noexcept
    {
        if (m_handle.promise().m_count.fetch_sub(1) == 1)
        {
            m_handle.destroy();
        }
    }
    void start()
    {
        // Increment the reference count BEFORE resuming: resume() may drive the
        // coroutine all the way to final_suspend, which would otherwise drop the
        // counter to zero and destroy the frame while start() is still using it.
        m_handle.promise().m_count.fetch_add(1);
        m_handle.resume();
    }

    std::atomic<WaitStatus>& getStatus() { return m_handle.promise().m_status; }

private:
    std::coroutine_handle<promise_type> m_handle;
};

}  // namespace bcos::task
