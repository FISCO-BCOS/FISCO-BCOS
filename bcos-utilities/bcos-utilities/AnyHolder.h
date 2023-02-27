#pragma once

#include "Error.h"
#include <bcos-utilities/Overloaded.h>
#include <boost/throw_exception.hpp>
#include <functional>
#include <type_traits>
#include <variant>

namespace bcos::utilities
{
// clang-format off
struct EmptyHolder: Error{};
// clang-format on

// Can hold both reference or value
template <class ValueType>
class AnyHolder
{
private:
    std::variant<std::monostate, ValueType, ValueType*, ValueType const*> m_value;

public:
    AnyHolder() : m_value(std::monostate()) {}
    AnyHolder(ValueType&& object) : m_value(std::forward<ValueType>(object)) {}
    AnyHolder(ValueType& object) : m_value(std::addressof(object)) {}
    AnyHolder(ValueType const& object) : m_value(std::addressof(object)) {}

    bool hasValue() const { return !std::holds_alternative<std::monostate>(m_value); }

    ValueType const& get() const&
    {
        return std::visit(overloaded{[](std::monostate const&) -> ValueType const& {
                                         BOOST_THROW_EXCEPTION(EmptyHolder{});
                                     },
                              [](ValueType const& ref) -> ValueType const& { return ref; },
                              [](ValueType& ref) -> ValueType const& { return ref; },
                              [](ValueType* ref) -> ValueType const& { return *ref; },
                              [](ValueType const* ref) -> ValueType const& { return *ref; }},
            m_value);
    }
};

}  // namespace bcos::utilities