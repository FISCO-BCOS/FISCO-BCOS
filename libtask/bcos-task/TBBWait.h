#pragma once

#include "Task.h"
#include "Trait.h"
#include <oneapi/tbb/task.h>
#include <exception>
#include <mutex>
#include <optional>
#include <type_traits>
#include <variant>

namespace bcos::task::tbb
{

// Better using inside tbb task
auto syncWait(auto&& task) -> AwaitableReturnType<std::remove_cvref_t<decltype(task)>>
    requires std::is_rvalue_reference_v<decltype(task)>
{
    using Task = std::remove_cvref_t<decltype(task)>;
    std::conditional_t<std::is_void_v<typename Task::ReturnType>,
        std::variant<std::monostate, std::exception_ptr>,
        std::variant<std::monostate, typename Task::ReturnType, std::exception_ptr>>
        value;

    std::mutex mutex;
    std::atomic_bool finished = false;
    std::atomic<oneapi::tbb::task::suspend_point> tbbTag;
    auto waitTask = [](Task&& task, decltype(value)& value, std::mutex& mutex,
                        std::atomic_bool& finished,
                        std::atomic<oneapi::tbb::task::suspend_point>& tbbTag) -> task::Task<void> {
        if constexpr (std::is_void_v<typename Task::ReturnType>)
        {
            co_await task;
        }
        else
        {
            value.template emplace<typename Task::ReturnType>(co_await task);
        }

        std::unique_lock<std::mutex> lock(mutex);
        finished = true;
        if (tbbTag)
        {
            lock.unlock();
            oneapi::tbb::task::resume(tbbTag);
        }
    }(std::forward<Task>(task), value, mutex, finished, tbbTag);

    waitTask.start();
    std::unique_lock<std::mutex> lock(mutex);
    if (!finished)
    {
        oneapi::tbb::task::suspend([&](oneapi::tbb::task::suspend_point tag) {
            tbbTag = tag;
            lock.unlock();
        });
    }

    if (std::holds_alternative<std::exception_ptr>(value))
    {
        std::rethrow_exception(std::get<std::exception_ptr>(value));
    }

    if constexpr (!std::is_void_v<typename Task::ReturnType>)
    {
        return std::move(std::get<typename Task::ReturnType>(value));
    }
};

}  // namespace bcos::task::tbb