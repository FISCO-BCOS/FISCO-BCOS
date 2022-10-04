#pragma once

#if __APPLE__ && __clang__
#include <experimental/coroutine>
namespace CO_STD = std::experimental;
#else
#include <coroutine>
namespace CO_STD = std;
#endif

namespace bcos::coroutine
{
template <class Awaitable>
concept MemberCoAwait = requires(Awaitable awaitable)
{
    awaitable.operator co_await;
};

template <class Awaitable>
concept NonMemberCoAwait = requires(Awaitable awaitable)
{
    operator co_await(awaitable);
};

template <class Awaitable>
concept CanCoAwait = requires(Awaitable awaitable)
{
    awaitable.await_ready();
};

template <class Object>
concept Awaitable = CO_STD::is_class_v<Object>;


}  // namespace bcos::coroutine