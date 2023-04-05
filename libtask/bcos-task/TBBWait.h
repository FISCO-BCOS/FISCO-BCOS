#pragma once

#include "Task.h"
#include "Trait.h"
#include <oneapi/tbb/task.h>
#include <exception>
#include <optional>
#include <type_traits>
#include <variant>

namespace bcos::task::tbb
{

// Better using inside tbb task
auto syncWait(auto&& task) -> AwaitableReturnType<std::remove_cvref_t<decltype(task)>>
    requires std::is_rvalue_reference_v<decltype(task)>
{
    using ReturnType = AwaitableReturnType<std::remove_cvref_t<decltype(task)>>;
    using Task = std::remove_cvref_t<decltype(task)>;
    std::conditional_t<std::is_void_v<ReturnType>, std::variant<std::monostate, std::exception_ptr>,
        std::variant<std::monostate, ReturnType, std::exception_ptr>>
        value;

    oneapi::tbb::task::suspend([&](oneapi::tbb::task::suspend_point tag) {
        auto waitTask = [](Task&& task, decltype(value)& value,
                            oneapi::tbb::task::suspend_point tag) -> task::Task<void> {
            try
            {
                if constexpr (std::is_void_v<typename Task::ReturnType>)
                {
                    co_await task;
                }
                else
                {
                    value.template emplace<ReturnType>(co_await task);
                }
            }
            catch (...)
            {
                value.template emplace<std::exception_ptr>(std::current_exception());
            }
            oneapi::tbb::task::resume(tag);
        }(std::forward<Task>(task), value, tag);
        waitTask.start();
    });

    if (std::holds_alternative<std::exception_ptr>(value))
    {
        std::rethrow_exception(std::get<std::exception_ptr>(value));
    }

    if constexpr (!std::is_void_v<ReturnType>)
    {
        return std::move(std::get<ReturnType>(value));
    }
}

}  // namespace bcos::task::tbb