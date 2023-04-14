#pragma once

#include <type_traits>

namespace bcos::task
{

template <class Task>
concept HasADLCoAwait = requires(Task&& task)
{
    operator co_await(task);
};
template <class Task>
concept HasMemberCoAwait = requires(Task&& task)
{
    task.operator co_await();
};
template <class Awaitable>
concept IsAwaitable = requires(Awaitable&& awaitable)
{
    awaitable.await_resume();
};

auto getAwaitable(auto&& task)
{
    using TaskType = std::remove_cvref_t<decltype(task)>;
    if constexpr (HasADLCoAwait<TaskType>)
    {
        return operator co_await(task);
    }
    else if constexpr (HasMemberCoAwait<TaskType>)
    {
        return task.operator co_await();
    }
    else if constexpr (IsAwaitable<TaskType>)
    {
        return task;
    }
    else
    {
        static_assert(!sizeof(TaskType*), "Not a valid task or an await_transform task!");
    }
}
template <class Task>
requires HasADLCoAwait<Task> || HasMemberCoAwait<Task> || IsAwaitable<Task>
struct AwaitableTrait
{
    using type = std::remove_cvref_t<decltype(getAwaitable(std::declval<Task>()))>;
};
template <class Task>
using AwaitableType = typename AwaitableTrait<Task>::type;

template <class Task>
struct AwaitableReturnTrait
{
    using type = decltype(std::declval<AwaitableType<Task>>().await_resume());
};
template <class Task>
using AwaitableReturnType = typename AwaitableReturnTrait<Task>::type;

}  // namespace bcos::task