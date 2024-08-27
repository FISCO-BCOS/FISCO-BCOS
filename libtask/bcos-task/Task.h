#pragma once
#include "bcos-concepts/Exception.h"
#include <boost/exception/diagnostic_information.hpp>
#include <boost/throw_exception.hpp>
#include <coroutine>
#include <exception>
#include <type_traits>
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

template <class VariantType>
struct Continuation
{
    std::coroutine_handle<> handle;
    VariantType value;
};

template <class Task, class PromiseImpl>
struct PromiseBase
{
    constexpr std::suspend_always initial_suspend() noexcept { return {}; }
    constexpr auto final_suspend() noexcept { return FinalAwaitable<PromiseImpl>{}; }
    Task get_return_object()
    {
        auto handle =
            std::coroutine_handle<PromiseImpl>::from_promise(*static_cast<PromiseImpl*>(this));
        return Task{handle};
    }
    void unhandled_exception()
    {
        if (!m_continuation)
        {
            auto handle =
                std::coroutine_handle<PromiseImpl>::from_promise(*static_cast<PromiseImpl*>(this));
            handle.destroy();
            std::rethrow_exception(std::current_exception());
        }
        m_continuation->value.template emplace<std::exception_ptr>(std::current_exception());
    }

    Continuation<typename Task::VariantType>* m_continuation{};
};

template <class Task>
struct PromiseVoid : public PromiseBase<Task, PromiseVoid<Task>>
{
    constexpr void return_void() noexcept {}
};

template <class Task>
struct PromiseValue : public PromiseBase<Task, PromiseValue<Task>>
{
    void return_value(auto&& value)
    {
        if (PromiseBase<Task, PromiseValue<Task>>::m_continuation)
        {
            PromiseBase<Task, PromiseValue<Task>>::m_continuation->value
                .template emplace<typename Task::Value>(std::forward<decltype(value)>(value));
        }
    }
};

template <class ValueType>
    requires(!std::is_reference_v<ValueType>)
class [[nodiscard]] Task
{
public:
    using Value = ValueType;
    using VariantType = std::conditional_t<std::is_void_v<ValueType>,
        std::variant<std::monostate, std::exception_ptr>,
        std::variant<std::monostate, ValueType, std::exception_ptr>>;

    using promise_type =
        std::conditional_t<std::is_same_v<Value, void>, PromiseVoid<Task>, PromiseValue<Task>>;

    struct Awaitable
    {
        explicit Awaitable(std::coroutine_handle<promise_type> handle)
          : m_handle(std::move(handle)) {};
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
        ValueType await_resume()
        {
            if (std::holds_alternative<std::exception_ptr>(m_continuation.value))
            {
                std::rethrow_exception(std::get<std::exception_ptr>(m_continuation.value));
            }

            if constexpr (!std::is_void_v<ValueType>)
            {
                if (!std::holds_alternative<Value>(m_continuation.value))
                {
                    BOOST_THROW_EXCEPTION(NoReturnValue{});
                }

                return std::move(std::get<Value>(m_continuation.value));
            }
        }

        std::coroutine_handle<promise_type> m_handle;
        Continuation<VariantType> m_continuation;
    };
    Awaitable operator co_await() const&& { return Awaitable(m_handle); }

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