#pragma once
#include <bcos-concepts/Coroutine.h>
#include <exception>

namespace bcos::task
{
struct Task
{
    struct Promise
    {
        constexpr CO_STD::suspend_never initial_suspend() const noexcept { return {}; }
        constexpr CO_STD::suspend_never final_suspend() const noexcept { return {}; }
        Task get_return_object()
        {
            return {CO_STD::coroutine_handle<Promise>::from_promise(*this)};
        }
        void return_void() {}
        void unhandled_exception() { std::rethrow_exception(std::current_exception()); }
    };

    using promise_type = Promise;

    constexpr std::suspend_never await_transform() const noexcept { return {}; }

    CO_STD::coroutine_handle<Promise> m_handle;
};


}  // namespace bcos::task