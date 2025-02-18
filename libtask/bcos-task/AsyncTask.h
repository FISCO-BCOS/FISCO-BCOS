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
#include <coroutine>
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

}  // namespace bcos::task
