#pragma once
#include "Scheduler.h"
#include "SequenceScheduler.h"
#include "Task.h"
#include <exception>
#include <future>
#include <iostream>

namespace bcos::task
{

void wait(
    auto&& task, Scheduler* scheduler = nullptr) requires std::is_rvalue_reference_v<decltype(task)>
{
    if (scheduler)
    {
        task.setScheduler(scheduler);
        scheduler->execute(task.handle());
    }
    else
    {
        task.handle().resume();
    }
}

auto syncWait(
    auto&& task, Scheduler* scheduler = nullptr) requires std::is_rvalue_reference_v<decltype(task)>
{
    using Task = std::remove_cvref_t<decltype(task)>;
    std::promise<typename Task::ReturnType> promise;
    auto future = promise.get_future();

    wait(
        [](Task task, std::promise<typename Task::ReturnType> promise) -> task::Task<void> {
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
        }(std::forward<Task>(task), std::move(promise)),
        scheduler);

    if constexpr (std::is_void_v<typename Task::ReturnType>)
    {
        future.get();
    }
    else
    {
        return future.get();
    }
}

template <class Task>
auto operator~(Task task)
{
    return syncWait(std::move(task));
}

}  // namespace bcos::task