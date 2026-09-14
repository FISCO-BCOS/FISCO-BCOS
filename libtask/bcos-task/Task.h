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
#include "bcos-utilities/Exceptions.h"
#include <atomic>
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
DERIVE_BCOS_EXCEPTION(NoReturnValue);

template <class Promise>
struct FinalAwaitable
{
    constexpr bool await_ready() noexcept { return false; }
    constexpr std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> handle) noexcept
    {
        return handle.promise().m_continuation ?
                   static_cast<std::coroutine_handle<>>(handle.promise().m_continuation->handle) :
                   std::noop_coroutine();
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
        if (m_continuation == nullptr)
        {
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

struct TaskPure
{
public:
    struct promise_type
    {
        constexpr std::suspend_always initial_suspend() noexcept { return {}; }
        constexpr std::suspend_never final_suspend() noexcept { return {}; }
        constexpr void return_void() noexcept {}
        void unhandled_exception()
        {
            std::rethrow_exception(std::current_exception());
        }
        TaskPure get_return_object()
        {
            auto handle = std::coroutine_handle<promise_type>::from_promise(
                *static_cast<promise_type*>(this));
            return TaskPure{handle};
        }
    };

    explicit TaskPure(std::coroutine_handle<promise_type> handle) : m_handle(handle) {}
    TaskPure(const TaskPure&) = delete;
    TaskPure(TaskPure&&) noexcept = default;
    TaskPure& operator=(const TaskPure&) = delete;
    TaskPure& operator=(TaskPure&&) noexcept = default;
    ~TaskPure() noexcept = default;
    const std::coroutine_handle<promise_type>& getHandle() const { return m_handle; }

private:
    std::coroutine_handle<promise_type> m_handle;
};

template <typename... Resp>
struct GetResultAwaitable
{
    // Shared state, single-shot: one Result backs exactly one co_await and exactly one
    // complete(). There is no reset — reuse would silently redeliver the first completion.
    struct Result
    {
        enum class State : uint8_t
        {
            INIT,
            SUSPENDED,
            DONE,
        };

        std::tuple<Resp...> data;
        // Suspend handshake only. DONE means the result arrived BEFORE the coroutine parked (so
        // await_ready() takes the fast path); a result delivered after it parked resumes the
        // coroutine and leaves this at SUSPENDED. Exactly-once is owned by `completed`, not here.
        std::atomic<State> state = State::INIT;
        std::coroutine_handle<> handle;
        // Exactly-once claim: the completer that flips this false->true owns the completion.
        std::atomic<bool> completed{false};
    };

    explicit GetResultAwaitable(Result& result) : m_result(result) {}

    bool await_ready() noexcept
    {
        // Fast path: the result was completed before the coroutine reached this co_await
        // (e.g. an ack arrived while the coroutine was still waiting on the write). Skip the
        // suspension entirely and let await_resume read the already-completed data.
        return m_result.state.load(std::memory_order_acquire) == Result::State::DONE;
    }
    bool await_suspend(std::coroutine_handle<> handle) noexcept
    {
        m_result.handle = handle;
        typename Result::State expected = Result::State::INIT;
        if (m_result.state.compare_exchange_strong(expected, Result::State::SUSPENDED,
                std::memory_order_acq_rel, std::memory_order_acquire))
        {
            return true;
        }
        // The completion won the race (state already DONE): do not suspend, await_resume reads
        // the data inline.
        return false;
    }
    std::tuple<Resp...> await_resume() noexcept
    {
        return std::move(m_result.data);
    }

    static void complete(Result& result, Resp... resp)
    {
        // Claim first: a second (or concurrent) complete() must not touch data or handle.
        auto claimed = false;
        if (!result.completed.compare_exchange_strong(claimed, true))
        {
            return;
        }
        // Write before publishing DONE, so a reader that observes DONE also observes data.
        typename Result::State expected = Result::State::INIT;
        result.data = std::make_tuple(std::move(resp)...);
        if (result.state.compare_exchange_strong(expected, Result::State::DONE,
                std::memory_order_acq_rel, std::memory_order_acquire))
        {
            return;
        }
        result.handle.resume();
    }

private:
    Result& m_result;
};

template <class TaskType>
struct Awaitable
{
    explicit Awaitable(std::coroutine_handle<typename TaskType::promise_type> handle)
      { m_continuation.handle = std::move(handle);}
    Awaitable(const Awaitable&) = delete;
    Awaitable(Awaitable&&) noexcept = default;
    Awaitable& operator=(const Awaitable&) = delete;
    Awaitable& operator=(Awaitable&&) noexcept = default;
    ~Awaitable() noexcept = default;

    bool await_ready() const noexcept { return !m_continuation.handle || m_continuation.handle.done(); }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle)
    {
        // This transformation operation is safe 
        // when Awaitable is constructed by coroutine_handle<typename TaskType::promise_type>
        // but unsafe when Awaitable is constructed by other ways
        auto nextHandle = 
            std::coroutine_handle<typename TaskType::promise_type>::from_address(m_continuation.handle.address());
        m_continuation.handle = handle;
        nextHandle.promise().m_continuation = std::addressof(m_continuation);
        return nextHandle;
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
        return *this;
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

}  // namespace bcos::task
