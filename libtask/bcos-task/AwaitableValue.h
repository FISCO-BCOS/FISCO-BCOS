#pragma once

#include <coroutine>
#include <utility>

namespace bcos::task
{

template <class Value>
class [[nodiscard]] AwaitableValue
{
public:
    AwaitableValue() = default;
    AwaitableValue(Value&& value) : m_value(std::forward<Value>(value)) {}
    constexpr static bool await_ready() noexcept { return true; }
    constexpr static bool await_suspend([[maybe_unused]] std::coroutine_handle<> handle) noexcept
    {
        return false;
    }
    constexpr Value await_resume() noexcept { return std::move(m_value); }
    const Value& value() const& noexcept { return m_value; }
    Value& value() & noexcept { return m_value; }
    Value toValue() && noexcept { return std::move(m_value); }

private:
    Value m_value;
};
template <>
struct [[nodiscard]] AwaitableValue<void>
{
    AwaitableValue() = default;
    static constexpr bool await_ready() noexcept { return true; }
    static constexpr bool await_suspend([[maybe_unused]] std::coroutine_handle<> handle) noexcept
    {
        return false;
    }
    constexpr void await_resume() noexcept {}
};
}  // namespace bcos::task