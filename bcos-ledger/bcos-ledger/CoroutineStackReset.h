/**
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
 * @file CoroutineStackReset.h
 * @brief Awaitable that resumes the coroutine on a dedicated worker with a
 *        fresh stack. Loops of inline-completing co_awaits (e.g. genesis
 *        alloc import against the legacy storage) nest one resume frame per
 *        iteration in build configurations where the symmetric-transfer tail
 *        call is not emitted, overflowing the thread stack on large loops; a
 *        genuine suspension is the only portable point where those frames
 *        unwind. The bounce is sequential: at any instant still exactly one
 *        thread runs the coroutine chain.
 */
#pragma once

#include <condition_variable>
#include <coroutine>
#include <mutex>
#include <queue>
#include <thread>

namespace bcos::ledger::detail
{
inline void postToStackResetWorker(std::coroutine_handle<> handle)
{
    struct Worker
    {
        std::mutex mutex;
        std::condition_variable cv;
        std::queue<std::coroutine_handle<>> pending;
        std::thread thread;

        Worker() : thread([this] { run(); }) {}
        void run()
        {
            for (;;)
            {
                std::coroutine_handle<> next;
                {
                    std::unique_lock lock(mutex);
                    cv.wait(lock, [this] { return !pending.empty(); });
                    next = pending.front();
                    pending.pop();
                }
                next.resume();
            }
        }
    };
    // Leaked on purpose: the worker must outlive every coroutine that could
    // still bounce off it, and there is nothing to join at process exit.
    static auto* worker = new Worker();
    {
        std::lock_guard lock(worker->mutex);
        worker->pending.push(handle);
    }
    worker->cv.notify_one();
}

struct StackReset
{
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> handle) const { postToStackResetWorker(handle); }
    void await_resume() const noexcept {}
};

inline constexpr StackReset stackReset{};
}  // namespace bcos::ledger::detail
