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
            try
            {
                std::rethrow_exception(std::current_exception());
            }
            catch (std::exception& e)
            {
                std::cout << boost::diagnostic_information(e) << std::endl;
            }
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
    auto future = promise.get_future();

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
                std::cout << std::this_thread::get_id() << "Set return exception" << std::endl;
                promise.set_exception(value);
            }
            else
            {
                std::cout << std::this_thread::get_id() << " Set return value: " << value
                          << std::endl;
                promise.set_value(std::forward<ValueType>(value));
            }
        });
    }

    std::cout << "Future get" << std::endl;
    return future.get();
}

template <class Task>
auto operator~(Task&& task)
{
    return syncWait(std::forward<Task>(task));
}

}  // namespace bcos::task