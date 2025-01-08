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
#include <boost/atomic/atomic_flag.hpp>
#include <exception>
#include <memory>
#include <type_traits>
#include <variant>

namespace bcos::task
{

constexpr inline struct Wait
{
    template <IsAwaitable Task>
    void operator()(Task&& task) const
    {
        std::forward<decltype(task)>(task).start();
    }
} wait{};

constexpr inline struct SyncWait
{
    template <IsAwaitable Task>
    auto operator()(
        Task&& task, auto&&... args) const -> AwaitableReturnType<std::remove_cvref_t<Task>>
    {
        using ReturnType = AwaitableReturnType<std::remove_cvref_t<Task>>;
        using ReturnTypeWrap = std::conditional_t<std::is_reference_v<ReturnType>,
            std::add_pointer_t<ReturnType>, ReturnType>;
        using ReturnVariant = std::conditional_t<std::is_void_v<ReturnType>,
            std::variant<std::monostate, std::exception_ptr>,
            std::variant<std::monostate, ReturnTypeWrap, std::exception_ptr>>;
        ReturnVariant result;
        boost::atomic_flag finished;
        boost::atomic_flag waitFlag;

        auto handle = [](Task&& task, decltype(result)& result, boost::atomic_flag& finished,
                          boost::atomic_flag& waitFlag,
                          auto&&... args) -> task::Task<void> {
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
                // 此处返回true说明外部首先设置了finished，那么需要通知外部已经执行完成了
                // If true is returned here, the external finish is set first, and the external
                // execution needs to be notified
                waitFlag.test_and_set();
                waitFlag.notify_one();
            }
        }(std::forward<Task>(task), result, finished, waitFlag,
                                              std::forward<decltype(args)>(args)...);
        handle.start();

        if (!finished.test_and_set())
        {
            // 此处返回false说明task还在执行中，需要等待task完成
            // If false is returned, the task is still being executed and you need to wait for the
            // task to complete
            waitFlag.wait(false);
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