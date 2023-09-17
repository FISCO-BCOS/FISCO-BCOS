#pragma once

#include "Task.h"
#include "Trait.h"
#include <oneapi/tbb/task.h>

namespace bcos::task
{

template <class Task>
auto syncWait(Task&& task) -> AwaitableReturnType<std::remove_cvref_t<Task>>
    requires IsAwaitable<Task> && std::is_rvalue_reference_v<decltype(task)>
{
    using ReturnType = AwaitableReturnType<std::remove_cvref_t<Task>>;
    using ReturnTypeWrap = std::conditional_t<std::is_reference_v<ReturnType>,
        std::add_pointer_t<ReturnType>, ReturnType>;
    using ReturnVariant = std::conditional_t<std::is_void_v<ReturnType>,
        std::variant<std::monostate, std::exception_ptr>,
        std::variant<std::monostate, ReturnTypeWrap, std::exception_ptr>>;
    ReturnVariant result;

    std::atomic_flag onceFlag;
    std::atomic<oneapi::tbb::task::suspend_point> suspendPoint;

    auto waitTask =
        [](Task&& task, decltype(result)& result, std::atomic_flag& onceFlag,
            std::atomic<oneapi::tbb::task::suspend_point>& suspendPoint) -> task::Task<void> {
        try
        {
            if constexpr (std::is_void_v<ReturnType>)
            {
                co_await task;
            }
            else
            {
                if constexpr (std::is_reference_v<ReturnType>)
                {
                    decltype(auto) ref = co_await task;
                    result = std::addressof(ref);
                }
                else
                {
                    result = co_await task;
                }
            }
        }
        catch (...)
        {
            result = std::current_exception();
        }

        if (!onceFlag.test_and_set())
        {
            suspendPoint.wait(oneapi::tbb::task::suspend_point{});
            tbb::task::resume(suspendPoint.load());
        }
    }(std::forward<Task>(task), result, onceFlag, suspendPoint);
    waitTask.start();

    if (onceFlag.test_and_set())
    {
        tbb::task::suspend([&](oneapi::tbb::task::suspend_point tag) {
            suspendPoint.store(tag);
            suspendPoint.notify_one();
        });
    }

    onceFlag.wait(false);
    if (std::holds_alternative<std::exception_ptr>(result))
    {
        std::rethrow_exception(std::get<std::exception_ptr>(result));
    }

    if constexpr (!std::is_void_v<ReturnType>)
    {
        if constexpr (std::is_reference_v<ReturnType>)
        {
            return *(std::get<ReturnTypeWrap>(result));
        }
        else
        {
            return std::move(std::get<ReturnTypeWrap>(result));
        }
    }
}

}  // namespace bcos::task