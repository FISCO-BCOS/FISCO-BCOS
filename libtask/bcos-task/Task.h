#pragma once
#include "Coroutine.h"
#include "Scheduler.h"
#include <bcos-concepts/Exception.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/throw_exception.hpp>
#include <concepts>
#include <exception>
#include <functional>
#include <type_traits>
#include <variant>

namespace bcos::task
{

// clang-format off
struct NoReturnValue : public bcos::error::Exception {};
// clang-format on

template <class Promise>
concept HasMemberScheduler = requires(Promise promise)
{
    promise.m_scheduler;
};

// Task可以在没有配置调度器的情况下自我调度，但这样的话每个Task将在彻底结束（清理协程栈之前）前恢复上一个Task的执行，导致Task的协程栈增长，一旦Task中有大量循环或深层次的协程co_await调用，将会导致协程栈溢出
// Tasks can be self-scheduled without a scheduler configured, but in this
// way, each Task will resume the execution of the previous Task before
// completely ending (before cleaning up the coroutine stack), causing the
// Task's coroutine stack to grow, and once there are a large number of
// loops or deep coroutine co_await calls in the Task, it will cause the
// coroutine stack to overflow
template <class Value>
class Task
{
public:
    using ReturnType = Value;
    struct PromiseVoid;
    struct PromiseValue;
    using promise_type = std::conditional_t<std::is_same_v<Value, void>, PromiseVoid, PromiseValue>;

    using ValueType =
        std::conditional_t<std::is_void_v<Value>, std::variant<std::monostate, std::exception_ptr>,
            std::variant<std::monostate, Value, std::exception_ptr>>;

    struct Awaitable
    {
        Awaitable(Task const& task) : m_handle(task.m_handle){};
        Awaitable(const Awaitable&) = delete;
        Awaitable(Awaitable&&) noexcept = default;
        Awaitable& operator=(const Awaitable&) = delete;
        Awaitable& operator=(Awaitable&&) noexcept = default;
        ~Awaitable() = default;

        bool await_ready() const noexcept { return !m_handle || m_handle.done(); }

        template <class Promise>
        void await_suspend(CO_STD::coroutine_handle<Promise> handle)
        {
            // 如果co_await来自有scheduler的task，沿用它的scheduler，结束时由该Scheduler执行continuation
            // If the co_await comes from a task with a scheduler, its scheduler is inherited and
            // the handle is executed using that scheduler
            if constexpr (HasMemberScheduler<Promise>)
            {
                if (handle.promise().m_scheduler)
                {
                    if (m_handle.promise().m_scheduler == nullptr)
                    {
                        m_handle.promise().m_scheduler = handle.promise().m_scheduler;
                    }
                }
            }

            m_handle.promise().m_continuationHandle = handle;
            m_handle.promise().m_awaitable = this;

            if (m_handle.promise().m_scheduler)
            {
                m_handle.promise().m_scheduler->execute(m_handle);
            }
            else
            {
                m_handle.resume();
            }

            // return m_handle;
        }
        Value await_resume()
        {
            auto& value = m_value;
            if (std::holds_alternative<std::exception_ptr>(value))
            {
                std::rethrow_exception(std::get<std::exception_ptr>(value));
            }

            if constexpr (!std::is_void_v<Value>)
            {
                if (!std::holds_alternative<Value>(value))
                {
                    BOOST_THROW_EXCEPTION(NoReturnValue{});
                }

                auto result = std::move(std::get<Value>(value));
                return result;
            }
        }

        CO_STD::coroutine_handle<promise_type> m_handle;
        ValueType m_value;
    };

    Awaitable operator co_await() { return Awaitable(*static_cast<Task*>(this)); }

    template <class PromiseImpl>
    struct PromiseBase
    {
        constexpr CO_STD::suspend_always initial_suspend() const noexcept { return {}; }
        constexpr auto final_suspend() noexcept
        {
            struct FinalAwaitable
            {
                constexpr bool await_ready() const noexcept { return false; }
                void await_suspend(CO_STD::coroutine_handle<PromiseImpl> handle) const noexcept
                {
                    auto continuationHandle = handle.promise().m_continuationHandle;
                    if (continuationHandle)
                    {
                        if (m_promise.m_scheduler)
                        {
                            m_promise.m_scheduler->execute(continuationHandle);
                        }
                        else
                        {
                            continuationHandle.resume();
                        }
                    }
                    handle.destroy();
                }
                constexpr void await_resume() noexcept {}

                PromiseBase& m_promise;
            };
            return FinalAwaitable{.m_promise = *this};
        }
        constexpr Task get_return_object()
        {
            auto handle = CO_STD::coroutine_handle<promise_type>::from_promise(
                *static_cast<PromiseImpl*>(this));
            return Task(handle);
        }
        void unhandled_exception()
        {
            if (m_awaitable)
            {
                m_awaitable->m_value.template emplace<std::exception_ptr>(std::current_exception());
            }
        }

        CO_STD::coroutine_handle<> m_continuationHandle;
        Awaitable* m_awaitable = nullptr;
        Scheduler* m_scheduler = nullptr;
    };
    struct PromiseVoid : public PromiseBase<PromiseVoid>
    {
        constexpr void return_void() noexcept {}
    };
    struct PromiseValue : public PromiseBase<PromiseValue>
    {
        template <class ReturnValue>
        void return_value(ReturnValue&& value)
        {
            if (PromiseBase<PromiseValue>::m_awaitable)
            {
                PromiseBase<PromiseValue>::m_awaitable->m_value.template emplace<Value>(
                    std::forward<ReturnValue>(value));
            }
        }
    };

    explicit Task(CO_STD::coroutine_handle<promise_type> handle) : m_handle(handle) {}
    Task(const Task&) = default;
    Task(Task&& task) noexcept : m_handle(task.m_handle) { task.m_handle = nullptr; }
    Task& operator=(const Task&) = default;
    Task& operator=(Task&& task) noexcept
    {
        m_handle = task.m_handle;
        task.m_handle = nullptr;
    }
    ~Task() noexcept = default;

    auto handle() noexcept { return m_handle; }
    void setScheduler(Scheduler* scheduler) noexcept { m_handle.promise().m_scheduler = scheduler; }

private:
    CO_STD::coroutine_handle<promise_type> m_handle;
};

template <class Value>
class AwaitableValue
{
public:
    AwaitableValue(Value&& value) : m_value(std::forward<Value>(value)) {}
    constexpr bool await_ready() const noexcept { return true; }
    constexpr bool await_suspend(CO_STD::coroutine_handle<> handle) const noexcept { return false; }
    constexpr Value await_resume() noexcept
    {
        if constexpr (!std::is_void_v<Value>)
        {
            return std::move(m_value);
        }
    }

    const Value& value() const { return m_value; }
    Value& value() { return m_value; }

private:
    Value m_value;
};
template <>
struct AwaitableValue<void>
{
    AwaitableValue() = default;
    static constexpr bool await_ready() noexcept { return true; }
    static constexpr bool await_suspend(CO_STD::coroutine_handle<> handle) noexcept
    {
        return false;
    }
    constexpr void await_resume() noexcept {}
};

}  // namespace bcos::task