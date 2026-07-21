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
#include <boost/atomic/atomic_flag.hpp>
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
        asyncTask.start();
    }
} wait{};

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
        // The task may complete on another thread. Everything that thread touches after
        // completion must be heap-owned (shared_ptr held by the detached coroutine below),
        // never on this waiter's stack: the waiter may wake, return and have its stack
        // unmapped while the completing thread is still executing the coroutine tail
        // (FIB-48 crash on macOS: "memory access violation ... no mapping at fault address").
        struct State
        {
            ReturnVariant m_result;
            boost::atomic_flag m_finished;
        };
        auto state = std::make_shared<State>();

        // AsyncTask self-destroys at final_suspend on whichever thread completes it, so the
        // waiter never destroys a coroutine frame the completer may still be executing in.
        auto executeTask = [](Task&& task, std::shared_ptr<State> state,
                               auto&&... args) -> AsyncTask {
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
                        state->m_result = std::addressof(ref);
                    }
                    else
                    {
                        state->m_result.template emplace<ReturnType>(
                            co_await std::forward<Task>(task));
                    }
                }
            }
            catch (...)
            {
                state->m_result.template emplace<std::exception_ptr>(std::current_exception());
            }

            state->m_finished.test_and_set();
            state->m_finished.notify_one();
        };
        executeTask(std::forward<Task>(task), state, std::forward<decltype(args)>(args)...).start();

        state->m_finished.wait(false);
        if (auto* exception = std::get_if<std::exception_ptr>(std::addressof(state->m_result)))
        {
            std::rethrow_exception(*exception);
        }

        if constexpr (!std::is_void_v<ReturnType>)
        {
            if constexpr (std::is_reference_v<ReturnType>)
            {
                return *(std::get<ReturnTypeWrap>(state->m_result));
            }
            else
            {
                return std::move(std::get<ReturnTypeWrap>(state->m_result));
            }
        }
    }
} syncWait{};

}  // namespace bcos::task
