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
        ReturnVariant result;

        auto handle = [](Task&& task, decltype(result)& result, auto&&... args) -> task::SyncTask {
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

            struct GetStatusAwaitable {
                std::coroutine_handle<SyncTask::promise_type> m_handle;
                bool await_ready() const noexcept { return false; }
                bool await_suspend(std::coroutine_handle<SyncTask::promise_type> handle) noexcept 
                {
                    m_handle = handle;
                    return false;
                }
                std::atomic<waitStatus>& await_resume() noexcept { return m_handle.promise().m_status; }
            };

            auto& status = co_await GetStatusAwaitable();

            auto expected = waitStatus::INIT;
            if (!status.compare_exchange_strong(expected, waitStatus::FINISHED))
            {
                // 此处返回true说明外部首先设置了finished，那么需要通知外部已经执行完成了
                // If true is returned here, the external finish is set first, and the external
                // execution needs to be notified
                status.store(waitStatus::FINISHED);
                status.notify_one();
            }
        }(std::forward<Task>(task), result, std::forward<decltype(args)>(args)...);
        auto& status = handle.getStatus();
        handle.start();

        auto expected = waitStatus::INIT;
        if (status.compare_exchange_strong(expected, waitStatus::WAITING))
        {
            // 此处返回true说明task还在执行中，需要等待task完成
            // If true is returned, the task is still being executed and you need to wait for the
            // task to complete
            status.wait(waitStatus::WAITING);
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
