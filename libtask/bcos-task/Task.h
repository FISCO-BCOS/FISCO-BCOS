#pragma once
#include "Coroutine.h"
#include <bcos-concepts/Exception.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/throw_exception.hpp>
#include <concepts>
#include <coroutine>
#include <exception>
#include <future>
#include <optional>
#include <type_traits>
#include <variant>

namespace bcos::task
{

// clang-format off
struct NoReturnValue : public bcos::error::Exception {};
// clang-format on

template <class TaskImpl, class Value>
class TaskBase
{
    // 如果Task没有配置调度器，每个Task将在彻底结束（清理栈之前）前恢复上一个Task的执行，导致Task的协程栈增长，一旦Task中有大量循环或深层次的协程co_await调用，将会导致协程栈溢出
    // If Task does not have a scheduler configured, each Task will resume the execution of the
    // previous Task before completely ending (before cleaning up the stack), causing the Task's
    // coroutine stack to grow, and once there are a large number of loops or deep coroutine
    // co_await calls in the Task, it will cause the coroutine stack to overflow

public:
    friend TaskImpl;
    using ReturnType = Value;
    struct PromiseVoid;
    struct PromiseValue;
    using promise_type = std::conditional_t<std::is_same_v<Value, void>, PromiseVoid, PromiseValue>;

    using ValueType =
        std::conditional_t<std::is_void_v<Value>, std::variant<std::monostate, std::exception_ptr>,
            std::variant<std::monostate, Value, std::exception_ptr>>;

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
                    CO_STD::coroutine_handle<> continuationHandle =
                        handle.promise().m_continuationHandle;
                    handle.destroy();

                    if (continuationHandle)
                    {
                        continuationHandle.resume();
                    }
                }
                constexpr void await_resume() noexcept {}
            };
            return FinalAwaitable{};
        }
        constexpr TaskImpl get_return_object()
        {
            auto handle = CO_STD::coroutine_handle<promise_type>::from_promise(
                *static_cast<PromiseImpl*>(this));
            return TaskImpl(handle);
        }
        void unhandled_exception()
        {
            if (m_value)
            {
                m_value->template emplace<std::exception_ptr>(std::current_exception());
            }
        }

        CO_STD::coroutine_handle<> m_continuationHandle;
        ValueType* m_value = nullptr;
    };
    struct PromiseVoid : public PromiseBase<PromiseVoid>
    {
        void return_void() {}
    };
    struct PromiseValue : public PromiseBase<PromiseValue>
    {
        template <class ReturnValue>
        void return_value(ReturnValue&& value)
        {
            if (PromiseBase<PromiseValue>::m_value)
            {
                PromiseBase<PromiseValue>::m_value->template emplace<Value>(
                    std::forward<ReturnValue>(value));
            }
        }
    };

    explicit TaskBase(CO_STD::coroutine_handle<promise_type> handle) : m_handle(handle) {}
    TaskBase(const TaskBase&) = delete;
    TaskBase(TaskBase&& task) noexcept : m_handle(task.m_handle) { task.m_handle = nullptr; }
    TaskBase& operator=(const TaskBase&) = delete;
    TaskBase& operator=(TaskBase&& task) noexcept
    {
        m_handle = task.m_handle;
        task.m_handle = nullptr;
    }
    ~TaskBase() noexcept = default;

    constexpr void run() { m_handle.resume(); }

private:
    CO_STD::coroutine_handle<promise_type> m_handle;
    ValueType m_promiseValue;  // Use to receive promise's value
};

template <class Value>
class Task : public TaskBase<Task<Value>, Value>
{
public:
    using TaskBase<Task<Value>, Value>::TaskBase;
    using typename TaskBase<Task<Value>, Value>::ReturnType;
    using typename TaskBase<Task<Value>, Value>::promise_type;
    using typename TaskBase<Task<Value>, Value>::ValueType;

    struct Awaitable
    {
        Awaitable(Task const& task) : m_handle(task.m_handle){};
        Awaitable(const Awaitable&) = delete;
        Awaitable(Awaitable&&) noexcept = default;
        Awaitable& operator=(const Awaitable&) = delete;
        Awaitable& operator=(Awaitable&&) noexcept = default;
        ~Awaitable() = default;

        constexpr bool await_ready() const noexcept { return !m_handle || m_handle.done(); }

        template <class Promise>
        auto await_suspend(CO_STD::coroutine_handle<Promise> handle)
        {
            m_handle.promise().m_continuationHandle = handle;
            m_handle.promise().m_value = &m_value;
            return m_handle;
        }
        constexpr Value await_resume()
        {
            // auto& value = m_handle.promise().m_value;
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

    friend Awaitable;
};

}  // namespace bcos::task