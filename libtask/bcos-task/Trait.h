#pragma once

#include <type_traits>
#include <utility>

namespace bcos::task
{

template <class Task>
concept HasADLCoAwait = requires(Task&& task) { operator co_await(task); };
template <class Task>
concept HasMemberCoAwait = requires(Task&& task) { std::forward<Task>(task).operator co_await(); };
template <class Awaitable>
concept HasAwaitable = requires(Awaitable&& awaitable) { awaitable.await_resume(); };

auto getAwaitable(auto&& task)
{
    using TaskType = std::remove_cvref_t<decltype(task)>;
    if constexpr (HasADLCoAwait<TaskType>)
    {
        return operator co_await(task);
    }
    else if constexpr (HasMemberCoAwait<TaskType>)
    {
        return std::forward<decltype(task)>(task).operator co_await();
    }
    else if constexpr (HasAwaitable<TaskType>)
    {
        return std::forward<decltype(task)>(task);
    }
    else
    {
        static_assert(!sizeof(TaskType*), "Not a valid task or an await_transform task!");
    }
}

template <class Awaitable>
concept IsAwaitable =
    HasADLCoAwait<Awaitable> || HasMemberCoAwait<Awaitable> || HasAwaitable<Awaitable>;

template <class Task>
    requires IsAwaitable<Task>
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