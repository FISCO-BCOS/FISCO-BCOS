#pragma once

#include <coroutine>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <tuple>

namespace bcos::storage::V2
{

struct Promise;
struct Coroutine : public std::coroutine_handle<Promise>
{
    using promise_type = Promise;
};

struct Promise
{
    Coroutine get_return_object() { return {Coroutine::from_promise(*this)}; }
    std::suspend_always initial_suspend() noexcept
    {
        std::cout << "[Promise] initial_suspend!" << std::endl;
        return {};
    }
    std::suspend_never final_suspend() noexcept
    {
        std::cout << "[Promise] final_suspend!" << std::endl;
        return {};
    }
    void return_void() {}
    std::suspend_always yield_value([[maybe_unused]] int a) { return {}; }
    void unhandled_exception() {}
};

}  // namespace bcos::storage::V2