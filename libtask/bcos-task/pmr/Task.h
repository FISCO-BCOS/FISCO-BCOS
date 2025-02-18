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
#include "../Task.h"
#include "MemoryResourceBase.h"

namespace bcos::task::pmr
{

template <class TaskType, class PromiseImpl>
struct PromiseBase : public task::PromiseBase<TaskType, PromiseImpl>, public MemoryResourceBase
{
};

template <class TaskType>
struct PromiseVoid : public PromiseBase<TaskType, PromiseVoid<TaskType>>
{
    constexpr void return_void() noexcept {}
};

template <class TaskType>
struct PromiseValue : public PromiseBase<TaskType, PromiseValue<TaskType>>
{
    void return_value(TaskType::Value value)
    {
        if (PromiseBase<TaskType, PromiseValue<TaskType>>::m_continuation)
        {
            PromiseBase<TaskType, PromiseValue<TaskType>>::m_continuation->value
                .template emplace<typename TaskType::Value>(std::move(value));
        }
    }
};

template <class ValueType>
    requires(!std::is_reference_v<ValueType>)
class [[nodiscard]] Task
{
public:
    using VariantValue = std::conditional_t<std::is_void_v<ValueType>,
        std::variant<std::monostate, std::exception_ptr>,
        std::variant<std::monostate, ValueType, std::exception_ptr>>;

    using Value = ValueType;
    using promise_type =
        std::conditional_t<std::is_same_v<ValueType, void>, PromiseVoid<Task>, PromiseValue<Task>>;

    task::Awaitable<Task> operator co_await() && { return task::Awaitable<Task>(m_handle); }

    explicit Task(std::coroutine_handle<promise_type> handle) : m_handle(handle) {}
    Task(const Task&) = delete;
    Task(Task&& task) noexcept : m_handle(task.m_handle) { task.m_handle = nullptr; }
    Task& operator=(const Task&) = delete;
    Task& operator=(Task&& task) noexcept
    {
        if (m_handle)
        {
            m_handle.destroy();
        }
        m_handle = task.m_handle;
        task.m_handle = nullptr;
    }
    ~Task() noexcept
    {
        if (m_handle)
        {
            m_handle.destroy();
        }
    }
    void start() { m_handle.resume(); }

private:
    std::coroutine_handle<promise_type> m_handle;
};

}  // namespace bcos::task::pmr