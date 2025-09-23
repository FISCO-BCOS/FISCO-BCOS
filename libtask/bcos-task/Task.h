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
#include "bcos-concepts/Exception.h"
#include <boost/exception/diagnostic_information.hpp>
#include <boost/throw_exception.hpp>
#include <coroutine>
#include <exception>
#include <memory>
#include <type_traits>
#include <utility>
#include <variant>

namespace bcos::task
{

struct NoReturnValue : public bcos::error::Exception
{
};

template <class Promise>
struct FinalAwaitable
{
    constexpr bool await_ready() noexcept { return false; }
    constexpr std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> handle) noexcept
    {
        std::coroutine_handle<> continuationHandle;
        if (handle.promise().m_continuation)
        {
            continuationHandle = handle.promise().m_continuation->handle;
        }
        handle.destroy();

        return continuationHandle ? continuationHandle : std::noop_coroutine();
    }
    constexpr void await_resume() noexcept {}
};

template <class VariantValue>
struct Continuation
{
    std::coroutine_handle<> handle;
    VariantValue value;
};

template <class TaskType, class PromiseImpl>
struct PromiseBase
{
    constexpr std::suspend_always initial_suspend() noexcept { return {}; }
    constexpr auto final_suspend() noexcept { return FinalAwaitable<PromiseImpl>{}; }
    TaskType get_return_object()
    {
        auto handle =
            std::coroutine_handle<PromiseImpl>::from_promise(*static_cast<PromiseImpl*>(this));
        return TaskType{handle};
    }
    void unhandled_exception()
    {
        auto exception = std::current_exception();
        if (!m_continuation)
        {
            auto handle =
                std::coroutine_handle<PromiseImpl>::from_promise(*static_cast<PromiseImpl*>(this));
            handle.destroy();
            std::rethrow_exception(exception);
        }
        m_continuation->value.template emplace<std::exception_ptr>(exception);
    }

    Continuation<typename TaskType::VariantValue>* m_continuation = nullptr;
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

template <class TaskType>
struct Awaitable
{
    explicit Awaitable(std::coroutine_handle<typename TaskType::promise_type> handle)
      : m_handle(std::move(handle)){};
    Awaitable(const Awaitable&) = delete;
    Awaitable(Awaitable&&) noexcept = default;
    Awaitable& operator=(const Awaitable&) = delete;
    Awaitable& operator=(Awaitable&&) noexcept = default;
    ~Awaitable() noexcept = default;

    bool await_ready() const noexcept { return !m_handle || m_handle.done(); }
    template <class Promise>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> handle)
    {
        m_continuation.handle = handle;
        m_handle.promise().m_continuation = std::addressof(m_continuation);
        return m_handle;
    }
    TaskType::Value await_resume()
    {
        if (auto* exception = std::get_if<std::exception_ptr>(std::addressof(m_continuation.value)))
        {
            std::rethrow_exception(*exception);
        }

        if constexpr (!std::is_void_v<typename TaskType::Value>)
        {
            if (auto* value =
                    std::get_if<typename TaskType::Value>(std::addressof(m_continuation.value)))
            {
                return std::move(*value);
            }
            BOOST_THROW_EXCEPTION(NoReturnValue{});
        }
    }

    std::coroutine_handle<typename TaskType::promise_type> m_handle;
    Continuation<typename TaskType::VariantValue> m_continuation;
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

    Awaitable<Task> operator co_await() && { return Awaitable<Task>(m_handle); }

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
    ~Task() noexcept = default;
    void start() { m_handle.resume(); }

private:
    std::coroutine_handle<promise_type> m_handle;
};

}  // namespace bcos::task
