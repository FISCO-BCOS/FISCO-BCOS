#pragma once

#include "Coroutine.h"
#include <utility>

namespace bcos::task
{

template <class Value>
class [[nodiscard]] AwaitableValue
{
public:
    AwaitableValue(Value value) : m_value(std::move(value)) {}
    constexpr static bool await_ready() noexcept { return true; }
    constexpr static bool await_suspend([[maybe_unused]] CO_STD::coroutine_handle<> handle) noexcept
    {
        return false;
    }
    constexpr Value await_resume() noexcept { return std::move(m_value); }
    const Value& value() const& { return m_value; }
    Value& value() & { return m_value; }
    Value toValue() && { return std::move(m_value); }

private:
    Value m_value;
};
template <>
struct AwaitableValue<void>
{
    AwaitableValue() = default;
    static constexpr bool await_ready() noexcept { return true; }
    static constexpr bool await_suspend([[maybe_unused]] CO_STD::coroutine_handle<> handle) noexcept
    {
        return false;
    }
    constexpr void await_resume() noexcept {}
};
}  // namespace bcos::task