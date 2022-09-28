#pragma once
#include "Task.h"
#include <exception>
#include <new>
#include <type_traits>
#include <variant>

namespace bcos::task
{

// template <IsTask Task, class Callback>
// class CallbackableTask
// {
// public:
//     CallbackableTask(Task task, Callback callback)
//       : m_task(std::move(task)), m_callback(std::move(callback))
//     {}

//     struct Promise
//     {
//         constexpr CO_STD::suspend_always initial_suspend() const noexcept { return {}; }
//         constexpr CO_STD::suspend_never final_suspend() const noexcept { return {}; }
//         constexpr CallbackableTask get_return_object()
//         {
//             auto task = Impl(CO_STD::coroutine_handle<Promise>::from_promise(*this));
//             m_task = &task;
//             return task;
//         }

//         CallbackableTask* m_task = nullptr;
//     };

//     using promise_type = Promise;

//     void get() { m_task.start(); }

//     void onFinalSuspend() { m_callback(Task::m_value); }

// private:
//     Task m_task;
//     Callback m_callback;
// };

template <class TaskType, class Callback>
task::Task<void> wait(TaskType task, Callback callback)
{
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
}

template <class TaskType>
typename TaskType::ReturnType syncWait(TaskType task)
{
    std::promise<typename TaskType::ReturnType> promise;
    wait(task, [&promise](auto&& value) {
        using ValueType = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::is_same_v<ValueType, std::exception_ptr>)
        {
            promise.set_exception(value);
        }
        else
        {
            if constexpr (std::is_void_v<ValueType>)
            {
                promise.set_value();
            }
            else
            {
                promise.set_value(std::forward<ValueType>(value));
            }
        }
    }).run();

    return promise.get_future().get();
}

}  // namespace bcos::task