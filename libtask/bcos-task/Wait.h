#pragma once
#include "Task.h"
#include "Trait.h"
#include <coroutine>
#include <exception>
#include <future>
#include <iostream>
#include <variant>

namespace bcos::task
{

class WaitTask
{
public:
    struct promise_type
    {
        CO_STD::suspend_always initial_suspend() noexcept { return {}; }
        auto final_suspend() noexcept
        {
            struct FinalAwaitable
            {
                bool await_ready() noexcept { return false; }
                void await_suspend(CO_STD::coroutine_handle<promise_type> handle) noexcept
                {
                    handle.destroy();
                }
                constexpr void await_resume() noexcept {}
            };
            return FinalAwaitable{};
        }
        auto get_return_object()
        {
            return CO_STD::coroutine_handle<promise_type>::from_promise(*this);
        }
        void unhandled_exception() {}
        void return_void() noexcept {}
    };

    WaitTask(CO_STD::coroutine_handle<promise_type> handle) noexcept : m_handle(handle) {}
    void start() { m_handle.resume(); }

private:
    CO_STD::coroutine_handle<promise_type> m_handle;
};

void wait(auto&& task) requires std::is_rvalue_reference_v<decltype(task)>
{
    auto waitTask = [](decltype(task) task) -> WaitTask {
        co_await task;
        co_return;
    }(std::forward<decltype(task)>(task));
    waitTask.start();
}

auto syncWait(auto&& task) requires std::is_rvalue_reference_v<decltype(task)>
{
    using Task = std::remove_cvref_t<decltype(task)>;
    std::promise<AwaitableReturnType<Task>> promise;
    auto future = promise.get_future();

    auto waitTask = [](Task&& task, std::promise<typename Task::ReturnType>& promise) -> WaitTask {
        try
        {
            if constexpr (std::is_void_v<typename Task::ReturnType>)
            {
                co_await task;
                promise.set_value();
            }
            else
            {
                promise.set_value(co_await task);
            }
        }
        catch (...)
        {
            promise.set_exception(std::current_exception());
        }

        co_return;
    }(std::forward<Task>(task), promise);
    waitTask.start();

    if constexpr (std::is_void_v<typename Task::ReturnType>)
    {
        future.get();
    }
    else
    {
        return future.get();
    }
}

template <IsAwaitable Task>
auto operator~(Task&& task)
{
    return syncWait(std::forward<Task>(task));
}

}  // namespace bcos::task