#pragma once
#include "Coroutine.h"
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

template <class Value>
requires(!std::is_rvalue_reference_v<Value>) class [[nodiscard]] Task
{
public:
    using ReturnType = Value;
    struct PromiseVoid;
    struct PromiseValue;
    using promise_type = std::conditional_t<std::is_same_v<Value, void>, PromiseVoid, PromiseValue>;

    constexpr static bool isReferenceValue = std::is_reference_v<Value>;
    using ValueType = std::conditional_t<std::is_reference_v<Value>,
        std::reference_wrapper<std::remove_reference_t<Value>>, Value>;
    using VariantType =
        std::conditional_t<std::is_void_v<Value>, std::variant<std::monostate, std::exception_ptr>,
            std::variant<std::monostate, ValueType, std::exception_ptr>>;

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
        auto await_suspend(CO_STD::coroutine_handle<Promise> handle)
        {
            m_handle.promise().m_continuationHandle = handle;
            m_handle.promise().m_awaitable = this;

            return m_handle;
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
                if (!std::holds_alternative<ValueType>(value))
                {
                    BOOST_THROW_EXCEPTION(NoReturnValue{});
                }

                auto result = std::move(std::get<ValueType>(value));
                if constexpr (isReferenceValue)
                {
                    return result.get();
                }
                return result;
            }
        }

        CO_STD::coroutine_handle<promise_type> m_handle;
        VariantType m_value;
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
                CO_STD::coroutine_handle<> await_suspend(
                    CO_STD::coroutine_handle<PromiseImpl> handle) const noexcept
                {
                    auto continuationHandle = handle.promise().m_continuationHandle;
                    handle.destroy();
                    return continuationHandle;
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
                PromiseBase<PromiseValue>::m_awaitable->m_value.template emplace<ValueType>(
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

private:
    CO_STD::coroutine_handle<promise_type> m_handle;
};

template <class Value>
class [[nodiscard]] AwaitableValue
{
public:
    AwaitableValue(Value&& value) : m_value(std::forward<Value>(value)) {}
    constexpr bool await_ready() const noexcept { return true; }
    constexpr bool await_suspend([[maybe_unused]] CO_STD::coroutine_handle<> handle) const noexcept
    {
        return false;
    }
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
    static constexpr bool await_suspend([[maybe_unused]] CO_STD::coroutine_handle<> handle) noexcept
    {
        return false;
    }
    constexpr void await_resume() noexcept {}
};

}  // namespace bcos::task