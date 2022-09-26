#pragma once
#include <bcos-concepts/Coroutine.h>
#include <bcos-concepts/Exception.h>
#include <boost/throw_exception.hpp>
#include <exception>
#include <type_traits>
#include <variant>

#include <iostream>

namespace bcos::task
{

// clang-format off
struct NoReturnValue : public bcos::exception::Exception {};
// clang-format on

template <class Value>
struct Task : public CO_STD::suspend_never
{
    struct PromiseVoid;
    struct PromiseValue;
    using promise_type = std::conditional_t<std::is_same_v<Value, void>, PromiseVoid, PromiseValue>;

    template <class Impl>
    struct PromiseBase
    {
        constexpr CO_STD::suspend_never initial_suspend() const noexcept { return {}; }
        constexpr CO_STD::suspend_never final_suspend() const noexcept { return {}; }
        constexpr Task get_return_object()
        {
            Task task(
                CO_STD::coroutine_handle<promise_type>::from_promise(*static_cast<Impl*>(this)));
            m_task = &task;  // Task lifetime is longer than promise
            return task;
        }
        constexpr void unhandled_exception()
        {
            m_task->m_value.template emplace<std::exception_ptr>(std::current_exception());
        }

        Task* m_task;
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

    Task(CO_STD::coroutine_handle<promise_type> handle) : m_handle(std::move(handle)) {}

    constexpr Value await_resume() { return getResult(); }

    constexpr Value getResult()
    {
        // Do not use m_handle, it's dead already
        if (std::holds_alternative<std::exception_ptr>(m_value))
            std::rethrow_exception(std::get<std::exception_ptr>(m_value));

        if constexpr (!std::is_same_v<Value, void>)
        {
            if (!std::holds_alternative<Value>(m_value))
                BOOST_THROW_EXCEPTION(NoReturnValue{});

            return std::move(std::get<Value>(m_value));
        }
    }

    CO_STD::coroutine_handle<promise_type> m_handle;
    std::conditional_t<std::is_same_v<Value, void>,
        std::variant<std::monostate, std::exception_ptr>,
        std::variant<std::monostate, Value, std::exception_ptr>>
        m_value;
};


}  // namespace bcos::task