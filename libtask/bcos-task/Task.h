#pragma once
#include "Coroutine.h"
#include "bcos-concepts/Exception.h"
#include <boost/exception/diagnostic_information.hpp>
#include <boost/throw_exception.hpp>
#include <exception>
#include <memory>
#include <type_traits>
#include <variant>

namespace bcos::task
{

struct NoReturnValue : public bcos::error::Exception
{
};

template <class Value>
    requires(!std::is_reference_v<Value>)
class [[nodiscard]] Task
{
public:
    using VariantType =
        std::conditional_t<std::is_void_v<Value>, std::variant<std::monostate, std::exception_ptr>,
            std::variant<std::monostate, Value, std::exception_ptr>>;

    struct Continuation
    {
        CO_STD::coroutine_handle<> handle;
        VariantType value;
    };

    template <class PromiseImpl>
    struct PromiseBase
    {
        constexpr CO_STD::suspend_always initial_suspend() noexcept { return {}; }
        constexpr auto final_suspend() noexcept
        {
            struct FinalAwaitable
            {
                constexpr bool await_ready() noexcept { return false; }
                constexpr CO_STD::coroutine_handle<> await_suspend(
                    CO_STD::coroutine_handle<PromiseImpl> handle) noexcept
                {
                    CO_STD::coroutine_handle<> continuationHandle;
                    if (handle.promise().m_continuation)
                    {
                        continuationHandle = handle.promise().m_continuation->handle;
                    }
                    handle.destroy();

                    return continuationHandle ? continuationHandle : CO_STD::noop_coroutine();
                }
                constexpr void await_resume() noexcept {}
            };
            return FinalAwaitable{};
        }
        Task get_return_object()
        {
            auto handle = CO_STD::coroutine_handle<PromiseImpl>::from_promise(
                *static_cast<PromiseImpl*>(this));
            return Task(handle);
        }
        void unhandled_exception()
        {
            if (!m_continuation)
            {
                std::rethrow_exception(std::current_exception());
            }
            m_continuation->value.template emplace<std::exception_ptr>(std::current_exception());
        }

        Continuation* m_continuation = nullptr;
    };
    struct PromiseVoid : public PromiseBase<PromiseVoid>
    {
        constexpr void return_void() noexcept {}
    };
    struct PromiseValue : public PromiseBase<PromiseValue>
    {
        void return_value(auto&& value)
        {
            if (PromiseBase<PromiseValue>::m_continuation)
            {
                PromiseBase<PromiseValue>::m_continuation->value.template emplace<Value>(
                    std::forward<decltype(value)>(value));
            }
        }
    };
    using promise_type = std::conditional_t<std::is_same_v<Value, void>, PromiseVoid, PromiseValue>;

    struct Awaitable
    {
        explicit Awaitable(Task const& task) : m_handle(task.m_handle){};
        Awaitable(const Awaitable&) = delete;
        Awaitable(Awaitable&&) noexcept = default;
        Awaitable& operator=(const Awaitable&) = delete;
        Awaitable& operator=(Awaitable&&) noexcept = default;
        ~Awaitable() noexcept = default;

        bool await_ready() const noexcept { return !m_handle || m_handle.done(); }
        template <class Promise>
        CO_STD::coroutine_handle<> await_suspend(CO_STD::coroutine_handle<Promise> handle)
        {
            m_continuation.handle = handle;
            m_handle.promise().m_continuation = std::addressof(m_continuation);
            return m_handle;
        }
        Value await_resume()
        {
            if (std::holds_alternative<std::exception_ptr>(m_continuation.value))
            {
                std::rethrow_exception(std::get<std::exception_ptr>(m_continuation.value));
            }

            if constexpr (!std::is_void_v<Value>)
            {
                if (!std::holds_alternative<Value>(m_continuation.value))
                {
                    BOOST_THROW_EXCEPTION(NoReturnValue{});
                }

                return std::move(std::get<Value>(m_continuation.value));
            }
        }

        CO_STD::coroutine_handle<promise_type> m_handle;
        Continuation m_continuation;
    };
    Awaitable operator co_await() { return Awaitable(*static_cast<Task*>(this)); }

    explicit Task(CO_STD::coroutine_handle<promise_type> handle) : m_handle(handle) {}
    Task(const Task&) = delete;
    Task(Task&& task) noexcept : m_handle(task.m_handle) { task.m_handle = nullptr; }
    Task& operator=(const Task&) = delete;
    Task& operator=(Task&& task) noexcept
    {
        m_handle = task.m_handle;
        task.m_handle = nullptr;
    }
    ~Task() noexcept = default;
    void start() { m_handle.resume(); }

private:
    CO_STD::coroutine_handle<promise_type> m_handle;
};

}  // namespace bcos::task