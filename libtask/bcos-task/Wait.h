#pragma once
#include "Task.h"
#include <exception>
#include <new>
#include <type_traits>
#include <variant>

namespace bcos::task
{

template <class Task, class Callback>
void wait(Task&& task, Callback callback)
{
    [&task, &callback]() -> task::Task<void> {
        using TaskType = std::remove_cvref_t<Task>;
        try
        {
            if constexpr (std::is_void_v<typename TaskType::ReturnType>)
            {
                co_await std::forward<TaskType>(task);
                callback();
            }
            else
            {
                callback(co_await std::forward<TaskType>(task));
            }
        }
        catch (...)
        {
            callback(std::current_exception());
        }
    }()
                                .run();
}

template <class Task>
void wait(Task&& task)
{
    std::move(task).run();
}

template <class Task>
auto syncWait(Task&& task)
{
    using TaskType = std::remove_cvref_t<Task>;
    std::promise<typename Task::ReturnType> promise;

    if constexpr (std::is_void_v<typename Task::ReturnType>)
    {
        wait(std::forward<TaskType>(task), [&promise](std::exception_ptr error = nullptr) {
            if (error)
            {
                promise.set_exception(error);
            }
            else
            {
                promise.set_value();
            }
        });
    }
    else
    {
        wait(std::forward<TaskType>(task), [&promise](auto&& value) {
            using ValueType = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::is_same_v<ValueType, std::exception_ptr>)
            {
                promise.set_exception(value);
            }
            else
            {
                promise.set_value(std::forward<ValueType>(value));
            }
        });
    }


    return promise.get_future().get();
}

template <class Task>
auto operator~(Task&& task)
{
    return syncWait(std::forward<Task>(task));
}

}  // namespace bcos::task