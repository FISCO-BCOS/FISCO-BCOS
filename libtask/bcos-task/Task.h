#pragma once
#include "Coroutine.h"
#include <bcos-concepts/Exception.h>
#include <boost/throw_exception.hpp>
#include <concepts>
#include <coroutine>
#include <exception>
#include <future>
#include <mutex>
#include <type_traits>
#include <variant>

namespace bcos::task
{

// clang-format off
struct NoReturnValue : public bcos::exception::Exception {};
// clang-format on

template <class Task>
concept HasFinalFunction = requires(Task task)
{
    task.onFinalSuspend();
};

template <class Value, class Impl>
class TaskBase
{
public:
    friend Impl;

    using ReturnType = Value;

    struct PromiseVoid;
    struct PromiseValue;
    using promise_type = std::conditional_t<std::is_same_v<Value, void>, PromiseVoid, PromiseValue>;

    template <class PromiseImpl>
    struct PromiseBase
    {
        constexpr CO_STD::suspend_always initial_suspend() const noexcept { return {}; }
        constexpr CO_STD::suspend_never final_suspend() const noexcept { return {}; }
        constexpr Impl get_return_object()
        {
            auto task = Impl(CO_STD::coroutine_handle<promise_type>::from_promise(
                *static_cast<PromiseImpl*>(this)));
            m_task = &task;
            return task;
        }
        constexpr void unhandled_exception()
        {
            m_task->m_value.template emplace<std::exception_ptr>(std::current_exception());
        }

        Impl* m_task = nullptr;
    };
    struct PromiseVoid : public PromiseBase<PromiseVoid>
    {
        void return_void() {}
    };
    struct PromiseValue : public PromiseBase<PromiseValue>
    {
        void return_value(Value&& value)
        {
            PromiseBase<PromiseValue>::m_task->m_value.template emplace<Value>(
                std::forward<Value>(value));
        }
    };

    TaskBase(CO_STD::coroutine_handle<promise_type>&& handle) : m_handle(std::move(handle)) {}


    constexpr void run() && { m_handle.resume(); }

private:
    constexpr Value result() &&
    {
        // Use m_value, assuming m_handle is dead
        auto& value = m_value;

        if (std::holds_alternative<std::exception_ptr>(value))
        {
            std::rethrow_exception(std::get<std::exception_ptr>(value));
        }

        if constexpr (!std::is_same_v<Value, void>)
        {
            if (!std::holds_alternative<Value>(value))
            {
                BOOST_THROW_EXCEPTION(NoReturnValue{});
            }

            return std::move(std::get<Value>(value));
        }
    }

    CO_STD::coroutine_handle<promise_type> m_handle;
    std::conditional_t<std::is_same_v<Value, void>,
        std::variant<std::monostate, std::exception_ptr>,
        std::variant<std::monostate, Value, std::exception_ptr>>
        m_value;
};

template <class Task>
concept IsTask = std::derived_from<Task, TaskBase<typename Task::ReturnType, Task>>;

template <class Value>
class Task : public TaskBase<Value, Task<Value>>
{
public:
    using TaskBase<Value, Task<Value>>::TaskBase;
    using typename TaskBase<Value, Task<Value>>::ReturnType;
    using typename TaskBase<Value, Task<Value>>::promise_type;

    template <class TaskType>
    struct Awaitable
    {
        Awaitable(TaskType& task) : m_task(task){};

        constexpr bool await_ready() const noexcept { return false; }
        auto await_suspend([[maybe_unused]] CO_STD::coroutine_handle<> handle)
        {
            m_task.m_handle.resume();
            return handle;
        }
        constexpr Value await_resume() { return static_cast<Task&&>(m_task).result(); }

        TaskType& m_task;
    };
    Awaitable<Task> operator co_await() && { return Awaitable<Task>(*static_cast<Task*>(this)); }
    friend Awaitable<Task>;
};

// template <class Value, class Callback>
// class SynchronalTask : public TaskBase<Value, SynchronalTask<Value, Callback>>
// {
// };

// template <class Value, class Executor>
// class ScheduledTask : public TaskBase<Value, Task<Value>>
// {
// public:
//     using TaskBase<Value, Task<Value>>::TaskBase;

//     struct Awaitable
//     {
//         Awaitable(ScheduledTask&& task) : m_task(std::move(task)){};

//         constexpr bool await_ready() const noexcept { return false; }
//         void await_suspend([[maybe_unused]] CO_STD::coroutine_handle<> handle) {}
//         constexpr Value await_resume() { return m_task.result(); }

//         ScheduledTask m_task;
//     };
//     Awaitable operator co_await() && { return Awaitable(std::move(*this)); }
//     friend Awaitable;

// private:
//     Executor* m_executor;
// };

// template <class Value>
// class WaitableTask : public TaskBase<Value, WaitableTask<Value>>
// {
// public:
//     constexpr static bool CAN_SCHEDULE = false;

// private:
//     std::future m_future;
// };

}  // namespace bcos::task