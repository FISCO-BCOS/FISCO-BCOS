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

#include "Task.h"
#include "Trait.h"
#include <oneapi/tbb/task.h>
#include <oneapi/tbb/task_group.h>
#include <boost/atomic/atomic.hpp>
#include <boost/atomic/atomic_flag.hpp>

namespace bcos::task::tbb
{

/*
bcos::task::tbb::syncWait仅在配合tbb::task_group使用时，才能有协程切换的效果，在下列函数中会阻塞外部线程，限制并发数：
- tbb::parallel_for
- tbb::parallel_for_each

bcos::task::tbb::syncWait can only have the effect of coroutine switching when used
with tbb::task_group, and will block external threads and limit the number of concurrent
transactions in the following functions
*/
constexpr inline struct SyncWait
{
    template <class Task>
    auto operator()(
        Task&& task, auto&&... args) const -> AwaitableReturnType<std::remove_cvref_t<Task>>
        requires IsAwaitable<Task>
    {
        using ReturnType = AwaitableReturnType<std::remove_cvref_t<Task>>;
        using ReturnTypeWrap = std::conditional_t<std::is_reference_v<ReturnType>,
            std::add_pointer_t<ReturnType>, ReturnType>;
        using ReturnVariant = std::conditional_t<std::is_void_v<ReturnType>,
            std::variant<std::monostate, std::exception_ptr>,
            std::variant<std::monostate, ReturnTypeWrap, std::exception_ptr>>;

        ReturnVariant result;
        boost::atomic_flag finished{};
        boost::atomic<oneapi::tbb::task::suspend_point> suspendPoint{};

        auto waitTask = [](Task&& task, decltype(result)& result, boost::atomic_flag& finished,
                            boost::atomic<oneapi::tbb::task::suspend_point>& suspendPoint,
                            auto&&...) -> task::Task<void> {
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

            if (finished.test_and_set())
            {
                // finished已经被设置,说明外部已经suspend了,此处要获取suspendPoint并resume
                // finished has been set, which means that the external has been suspended, here you
                // need to get suspendPoint and resume
                suspendPoint.wait({});
                oneapi::tbb::task::resume(suspendPoint.load());
            }
        }(std::forward<Task>(task), result, finished, suspendPoint,
                                           std::forward<decltype(args)>(args)...);
        waitTask.start();

        if (!finished.test_and_set())
        {
            // finished第一次被设置,说明task还在执行中,suspend并等待task来执行resume
            // finished is set for the first time, indicating that the task is still being executed,
            // suspending and waiting for the task to execute resume
            oneapi::tbb::task::suspend([&](oneapi::tbb::task::suspend_point tag) {
                suspendPoint.store(tag);
                suspendPoint.notify_one();
            });
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

}  // namespace bcos::task::tbb