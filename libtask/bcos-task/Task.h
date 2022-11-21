#pragma once
#include "Coroutine.h"
#include <bcos-concepts/Exception.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/throw_exception.hpp>
#include <concepts>
#include <exception>
#include <future>
#include <mutex>
#include <type_traits>
#include <variant>

#include <iostream>

namespace bcos::task
{

// clang-format off
struct NoReturnValue : public bcos::error::Exception {};
// clang-format on

template <class TaskImpl, class Value>
class TaskBase
{
public:
    friend TaskImpl;

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
                constexpr bool await_ready() const noexcept { return false; }
                void await_suspend(CO_STD::coroutine_handle<PromiseImpl> handle) noexcept
                {
                    if (handle.promise().m_continuationHandle)
                    {
                        handle.promise().m_continuationHandle.resume();
                    }
                    handle.destroy();
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
            m_value.template emplace<std::exception_ptr>(std::current_exception());
        }

        CO_STD::coroutine_handle<> m_continuationHandle;
        std::conditional_t<std::is_void_v<Value>, std::variant<std::monostate, std::exception_ptr>,
            std::variant<std::monostate, Value, std::exception_ptr>>
            m_value;
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
            PromiseBase<PromiseValue>::m_value.template emplace<Value>(
                std::forward<ReturnValue>(value));
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
    ~TaskBase() = default;

    constexpr void run() { m_handle.resume(); }

private:
    CO_STD::coroutine_handle<promise_type> m_handle;
};

enum class Type
{
    LAZY,
    EAGER
};

template <class Value, Type type = Type::LAZY>
class Task : public TaskBase<Task<Value, type>, Value>
{
public:
    using TaskBase<Task<Value, type>, Value>::TaskBase;
    using typename TaskBase<Task<Value, type>, Value>::ReturnType;
    using typename TaskBase<Task<Value, type>, Value>::promise_type;

    struct Awaitable
    {
        Awaitable(Task const& task) : m_handle(task.m_handle){};
        Awaitable(const Awaitable&) = delete;
        Awaitable(Awaitable&&) noexcept = default;
        Awaitable& operator=(const Awaitable&) = delete;
        Awaitable& operator=(Awaitable&&) noexcept = default;
        ~Awaitable() = default;

        constexpr bool await_ready() const noexcept
        {
            return type == Type::EAGER || !m_handle || m_handle.done();
        }

        template <class Promise>
        auto await_suspend(CO_STD::coroutine_handle<Promise> handle)
        {
            m_handle.promise().m_continuationHandle = handle;
            return m_handle;
        }
        constexpr Value await_resume()
        {
            auto& value = m_handle.promise().m_value;
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
    };
    Awaitable operator co_await() { return Awaitable(*static_cast<Task*>(this)); }

    constexpr bool lazy() const { return type == Type::LAZY; }

    friend Awaitable;
};

}  // namespace bcos::task