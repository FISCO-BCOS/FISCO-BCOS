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

#include <type_traits>
#include <utility>

namespace bcos::task
{

template <class Task>
concept HasADLCoAwait = requires(Task&& task) { operator co_await(task); };
template <class Task>
concept HasMemberCoAwait = requires(Task&& task) { std::forward<Task>(task).operator co_await(); };
template <class Awaitable>
concept HasAwaitable = requires(Awaitable&& awaitable) { awaitable.await_resume(); };

auto getAwaitable(auto&& task)
{
    using TaskType = std::remove_cvref_t<decltype(task)>;
    if constexpr (HasADLCoAwait<TaskType>)
    {
        return operator co_await(task);
    }
    else if constexpr (HasMemberCoAwait<TaskType>)
    {
        return std::forward<decltype(task)>(task).operator co_await();
    }
    else if constexpr (HasAwaitable<TaskType>)
    {
        return std::forward<decltype(task)>(task);
    }
    else
    {
        static_assert(!sizeof(TaskType*), "Not a valid task or an await_transform task!");
    }
}

template <class Awaitable>
concept IsAwaitable =
    HasADLCoAwait<Awaitable> || HasMemberCoAwait<Awaitable> || HasAwaitable<Awaitable>;

template <class Task>
    requires IsAwaitable<Task>
struct AwaitableTrait
{
    using type = std::remove_cvref_t<decltype(getAwaitable(std::declval<Task>()))>;
};
template <class Task>
using AwaitableType = typename AwaitableTrait<Task>::type;

template <class Task>
struct AwaitableReturnTrait
{
    using type = decltype(std::declval<AwaitableType<Task>>().await_resume());
};
template <class Task>
using AwaitableReturnType = typename AwaitableReturnTrait<Task>::type;

template <class ReturnValue, class ValueType>
concept IsAwaitableReturnValue = std::same_as<task::AwaitableReturnType<ReturnValue>, ValueType>;

}  // namespace bcos::task