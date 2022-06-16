#pragma once

#include <coroutine>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <tuple>

namespace bcos::storage::V2
{

template <typename T>
struct Coroutine
{
    struct Promise
    {
        Coroutine get_return_object() { return {}; }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        T return_value(T value) { return value; }
        void unhandled_exception() {}
    };

    using promise_type = Promise;
    using handler_type = std::coroutine_handle<Promise>;

    bool await_ready()
    {
        std::cout << "await_ready..." << std::endl;
        return false;
    }
    void await_suspend(std::coroutine_handle<Promise> handle)
    {
        std::cout << "await_suspend..." << std::endl;
        handle.resume();
    }
    T await_resume() { return T{}; }
};

class Storage
{
public:
    using TableKey = std::tuple<std::string_view, std::string_view>;

    Coroutine<std::string> getRow([[maybe_unused]] TableKey key)
    {
        auto it = m_store.find(key);
        if (it != m_store.end())
        {
            co_return it->second;
        }
        else
        {
            co_return std::string{"Not found!"};
        }
    }

private:
    std::map<TableKey, std::string> m_store;
};
}  // namespace bcos::storage::V2