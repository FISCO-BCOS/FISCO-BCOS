#pragma once
#include "MonoScheduler.h"
#include "Scheduler.h"
#include "Task.h"
#include "bcos-utilities/Overloaded.h"
#include <boost/exception/diagnostic_information.hpp>
#include <coroutine>
#include <exception>
#include <type_traits>
#include <variant>

namespace bcos::task
{

struct Success
{
};

template <class Value>
using AsyncWaitResult = std::conditional_t<std::is_void_v<Value>,
    std::variant<Success, std::exception_ptr>, std::variant<std::exception_ptr, Value>>;

void wait(
    auto&& task, auto&& callback = []([[maybe_unused]] auto result) noexcept {},
    Scheduler& scheduler = MonoScheduler::instance) requires
    std::is_rvalue_reference_v<decltype(task)> && std::is_rvalue_reference_v<decltype(callback)> &&
    std::is_nothrow_invocable_v<decltype(callback),
        AsyncWaitResult<typename std::remove_cvref_t<decltype(task)>::ReturnType>>
{
    auto waitTask = [](auto&& task, auto&& callback) -> task::Task<void> {
        using TaskType = std::remove_cvref_t<decltype(task)>;
        try
        {
            if constexpr (std::is_void_v<typename TaskType::ReturnType>)
            {
                co_await task;
                callback(Success{});
            }
            else
            {
                callback(co_await task);
            }
        }
        catch (...)
        {
            callback(std::current_exception());
        }

        co_return;
    }(std::forward<decltype(task)>(task), std::forward<decltype(callback)>(callback));

    if (std::addressof(scheduler) == std::addressof(MonoScheduler::instance))
    {
        auto monoWrapper = [](decltype(waitTask) task) -> task::Task<void> {
            MonoScheduler monoScheduler;
            task.setScheduler(&monoScheduler);

            co_await task;
        }(std::move(waitTask));
        monoWrapper.handle().resume();
    }
    else
    {
        scheduler.execute(waitTask.handle());
    }
}

auto syncWait(auto&& task, Scheduler& scheduler = MonoScheduler::instance) requires
    std::is_rvalue_reference_v<decltype(task)>
{
    using Task = std::remove_cvref_t<decltype(task)>;
    std::promise<typename Task::ReturnType> promise;
    auto future = promise.get_future();

    if constexpr (std::is_void_v<typename Task::ReturnType>)
    {
        wait(
            std::forward<decltype(task)>(task),
            [&promise](auto&& result) noexcept {
                std::visit(bcos::overloaded{[&promise](Success) { promise.set_value(); },
                               [&promise](std::exception_ptr error) {
                                   promise.set_exception(std::move(error));
                               }},
                    result);
            },
            scheduler);
        future.get();
    }
    else
    {
        wait(
            std::forward<decltype(task)>(task),
            [&promise](auto&& result) mutable noexcept -> void {
                std::visit(
                    bcos::overloaded{[&promise](typename Task::ReturnType& value) {
                                         promise.set_value(std::forward<decltype(value)>(value));
                                     },
                        [&promise](std::exception_ptr error) { promise.set_exception(error); }},
                    result);
            },
            scheduler);
        return future.get();
    }
}

template <class Task>
auto operator~(Task task)
{
    return syncWait(std::move(task));
}

}  // namespace bcos::task