#pragma once
#include "Task.h"
#include <boost/exception/diagnostic_information.hpp>
#include <exception>
#include <new>
#include <thread>
#include <type_traits>
#include <variant>

namespace bcos::task
{

template <class Task, class Callback>
void wait(Task&& task, Callback&& callback)
{
    auto waitTask = [](Task&& task, Callback&& callback) -> task::Task<void> {
        using TaskType = std::remove_cvref_t<Task>;
        try
        {
            if constexpr (std::is_void_v<typename TaskType::ReturnType>)
            {
                co_await std::move(task);
                callback();
            }
            else
            {
                callback(co_await std::move(task));
            }
        }
        catch (...)
        {
            callback(std::current_exception());
        }
    };
    waitTask(std::forward<Task>(task), std::forward<Callback>(callback)).run();
}

template <class Task>
void wait(Task&& task)
{
    std::forward<Task>(task).run();
}

template <class Task>
auto syncWait(Task&& task)
{
    std::promise<typename Task::ReturnType> promise;
    auto future = promise.get_future();

    if constexpr (std::is_void_v<typename Task::ReturnType>)
    {
        wait(std::forward<Task>(task), [&promise](std::exception_ptr error = nullptr) {
            if (error)
            {
                promise.set_exception(error);
            }
            else
            {
                promise.set_value();
            }
        });
        future.get();
    }
    else
    {
        wait(std::forward<Task>(task), [&promise](auto&& value) mutable -> void {
            using ValueType = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::is_same_v<ValueType, std::exception_ptr>)
            {
                promise.set_exception(value);
            }
            else
            {
                promise.set_value(std::forward<decltype(value)>(value));
            }
        });
        return future.get();
    }
}

template <class Task>
auto operator~(Task&& task)
{
    return syncWait(std::forward<Task>(task));
}

}  // namespace bcos::task