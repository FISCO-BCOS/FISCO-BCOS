#pragma once
#include "Coroutine.h"
#include "Scheduler.h"
#include <bcos-concepts/Exception.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/throw_exception.hpp>
#include <concepts>
#include <exception>
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
        auto await_suspend(CO_STD::coroutine_handle<Promise> handle)
        {
            // 如果co_await来自有scheduler的task，沿用它的scheduler，并且使用该scheduler来执行handle
            // If the co_await comes from a task with a scheduler, its scheduler is inherited and
            // the handle is executed using that scheduler
            if constexpr (HasMemberScheduler<Promise>)
            {
                if (handle.promise().m_scheduler)
                {
                    m_handle.promise().m_scheduler = handle.promise().m_scheduler;
                }
            }

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
                        // Task可以在没有配置调度器的情况下自我调度，但这样的话每个Task将在彻底结束（清理协程栈之前）前恢复上一个Task的执行，导致Task的协程栈增长，一旦Task中有大量循环或深层次的协程co_await调用，将会导致协程栈溢出
                        // Tasks can be self-scheduled without a scheduler configured, but in this
                        // way, each Task will resume the execution of the previous Task before
                        // completely ending (before cleaning up the coroutine stack), causing the
                        // Task's coroutine stack to grow, and once there are a large number of
                        // loops or deep coroutine co_await calls in the Task, it will cause the
                        // coroutine stack to overflow
                        if (m_scheduler != nullptr)
                        {
                            m_scheduler->execute(continuationHandle);
                        }
                        else
                        {
                            continuationHandle.resume();
                        }
                    }
                    handle.destroy();
                }
                constexpr void await_resume() noexcept {}

                Scheduler* m_scheduler = nullptr;
            };
            return FinalAwaitable{.m_scheduler = m_scheduler};
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
        Scheduler* m_scheduler = nullptr;
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

}  // namespace bcos::task