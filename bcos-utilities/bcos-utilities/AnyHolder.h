#pragma once

#include "Error.h"
#include <bcos-utilities/Overloaded.h>
#include <boost/throw_exception.hpp>
#include <functional>
#include <variant>

namespace bcos::utilities
{

// clang-format off
struct ReferenceFromConstError: Error {};
// clang-format on

// Can hold both reference or value
template <class Value>
class AnyHolder
{
private:
    std::variant<Value, std::reference_wrapper<Value>, std::reference_wrapper<Value const>> m_value;

public:
    AnyHolder(Value&& object) : m_value(std::forward<Value>(object)) {}
    AnyHolder(Value& object) : m_value(std::reference_wrapper<Value>(object)) {}
    AnyHolder(Value const& object) : m_value(std::reference_wrapper<Value const>(object)) {}

    Value& get() &
    {
        return std::visit(
            overloaded([](Value& ref) -> Value& { return ref; },
                [](std::reference_wrapper<Value>& ref) -> Value& { return ref.get(); },
                [](std::reference_wrapper<Value const> ref) -> Value& {
                    BOOST_THROW_EXCEPTION(ReferenceFromConstError{});
                }),
            m_value);
    }
    Value const& get() const&
    {
        return std::visit(
            overloaded([](Value& ref) -> Value const& { return ref; },
                [](std::reference_wrapper<Value>& ref) -> Value const& { return ref.get(); },
                [](std::reference_wrapper<Value const> ref) -> Value const& { return ref.get(); }),
            m_value);
    }

    Value& operator()() & { return get(); }
    Value const& operator()() const& { return get(); }
};

}  // namespace bcos::utilities