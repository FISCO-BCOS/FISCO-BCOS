#pragma once
#include "Task.h"
#include "Trait.h"
#include <exception>
#include <future>

namespace bcos::task
{

void wait(auto&& task) requires std::is_rvalue_reference_v<decltype(task)>
{
    task.start();
}

auto syncWait(auto&& task) -> AwaitableReturnType<std::remove_cvref_t<decltype(task)>>
requires std::is_rvalue_reference_v<decltype(task)>
{
    using Task = std::remove_cvref_t<decltype(task)>;
    std::promise<AwaitableReturnType<Task>> promise;
    auto future = promise.get_future();

    auto waitTask = [](Task&& task,
                        std::promise<typename Task::ReturnType>& promise) -> task::Task<void> {
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