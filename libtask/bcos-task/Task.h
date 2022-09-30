#pragma once
#include "Coroutine.h"
#include <bcos-concepts/Exception.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/throw_exception.hpp>
#include <concepts>
#include <coroutine>
#include <exception>
#include <future>
#include <mutex>
#include <type_traits>
#include <variant>

#include <iostream>

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

template <class Impl, class Value>
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
        constexpr auto final_suspend() const noexcept
        {
            struct FinalAwaitable
            {
                constexpr bool await_ready() const noexcept { return !m_continuationHandle; }
                auto await_suspend([[maybe_unused]] CO_STD::coroutine_handle<> handle) noexcept
                {
                    return m_continuationHandle;
                }
                constexpr void await_resume() noexcept {}

                std::coroutine_handle<> m_continuationHandle;
            };

            return FinalAwaitable{std::move(m_task->m_continuationHandle)};
        }
        constexpr Impl get_return_object()
        {
            auto task = Impl(CO_STD::coroutine_handle<promise_type>::from_promise(
                *static_cast<PromiseImpl*>(this)));
            m_task = &task;
            return task;
        }
        constexpr void unhandled_exception()
        {
            auto e = std::current_exception();
            try
            {
                std::rethrow_exception(e);
            }
            catch (std::exception& error)
            {
                std::cout << std::this_thread::get_id() << "---"
                          << boost::diagnostic_information(error) << std::endl;
            }
            m_task->m_value.template emplace<std::exception_ptr>(e);
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
        if (std::holds_alternative<std::exception_ptr>(m_value))
        {
            std::rethrow_exception(std::get<std::exception_ptr>(m_value));
        }

        if constexpr (!std::is_void_v<Value>)
        {
            if (!std::holds_alternative<Value>(m_value))
            {
                BOOST_THROW_EXCEPTION(NoReturnValue{});
            }

            return std::move(std::get<Value>(m_value));
        }
    }

    CO_STD::coroutine_handle<promise_type> m_handle;
    CO_STD::coroutine_handle<> m_continuationHandle;

    std::conditional_t<std::is_void_v<Value>, std::variant<std::monostate, std::exception_ptr>,
        std::variant<std::monostate, Value, std::exception_ptr>>
        m_value;
};

template <class Task>
concept IsTask = std::derived_from<Task, TaskBase<typename Task::ReturnType, Task>>;

enum class Type
{
    LAZY,
    EAGER
};
template <class Value, Type type = Type::LAZY>
class Task : public TaskBase<Task<Value, type>, Value>
{
public:
    using TaskBase<Task<Value>, Value>::TaskBase;
    using typename TaskBase<Task<Value, type>, Value>::ReturnType;
    using typename TaskBase<Task<Value, type>, Value>::promise_type;

    template <class TaskType>
    struct Awaitable
    {
        Awaitable(TaskType& task) : m_task(task){};

        constexpr bool await_ready() const noexcept { return type == Type::EAGER; }
        auto await_suspend(CO_STD::coroutine_handle<> handle)
        {
            m_task.m_continuationHandle = std::move(handle);
            return m_task.m_handle;
        }
        constexpr Value await_resume() { return static_cast<Task&&>(m_task).result(); }

        TaskType& m_task;
    };
    Awaitable<Task> operator co_await() && { return Awaitable<Task>(*static_cast<Task*>(this)); }
    friend Awaitable<Task>;
};

}  // namespace bcos::task