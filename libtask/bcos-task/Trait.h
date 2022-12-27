#pragma once

#include <type_traits>

namespace bcos::task
{

template <class Awaitable>
concept HasADLCoAwait = requires(Awaitable&& awaitable)
{
    operator co_await(awaitable);
};
template <class Awaitable>
concept HasMemberCoAwait = requires(Awaitable&& awaitable)
{
    awaitable.operator co_await();
};

auto getAwaiter(auto&& awaitable)
{
    using AwaitableType = std::remove_cvref_t<decltype(awaitable)>;
    if constexpr (HasADLCoAwait<AwaitableType>)
    {
        return operator co_await(awaitable);
    }
    else if constexpr (HasMemberCoAwait<AwaitableType>)
    {
        return awaitable.operator co_await();
    }
    else
    {
        static_assert(
            !sizeof(AwaitableType*), "Not a valid awaitable or an await_transform awaitable!");
    }
}
template <class Awaitable>
requires HasADLCoAwait<Awaitable> || HasMemberCoAwait<Awaitable>
struct AwaiterTrait
{
    using type = std::remove_cvref_t<decltype(getAwaiter(std::declval<Awaitable>()))>;
};
template <class Awaitable>
using AwaiterType = typename AwaiterTrait<Awaitable>::type;

template <class Awaitable>
struct AwaiterReturnTrait
{
    using type = decltype(std::declval<AwaiterType<Awaitable>>().await_resume());
};
template <class Awaitable>
using AwaiterReturnType = typename AwaiterReturnTrait<Awaitable>::type;

}  // namespace bcos::task