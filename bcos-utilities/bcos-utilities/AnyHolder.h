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
struct ReferenceFromConstError: Error {};
// clang-format on

// Can hold both reference or value
template <class Value>
class AnyHolder
{
private:
    using ValueType = std::remove_cvref_t<Value>;
    std::variant<ValueType, ValueType const, std::reference_wrapper<ValueType>,
        std::reference_wrapper<ValueType const>>
        m_value;

public:
    AnyHolder(ValueType&& object) : m_value(std::forward<Value>(object)) {}
    AnyHolder(ValueType& object) : m_value(std::reference_wrapper<ValueType>(object)) {}
    AnyHolder(ValueType const& object) : m_value(std::reference_wrapper<ValueType const>(object)) {}

    ValueType& get() & requires(!std::is_const_v<std::remove_reference_t<Value>>)
    {
        return std::visit(
            overloaded([](ValueType& ref) -> ValueType& { return ref; },
                [](ValueType const& ref) -> ValueType& {
                    BOOST_THROW_EXCEPTION(ReferenceFromConstError{});
                },
                [](std::reference_wrapper<ValueType>& ref) -> ValueType& { return ref.get(); },
                [](std::reference_wrapper<ValueType const>& ref) -> ValueType& {
                    BOOST_THROW_EXCEPTION(ReferenceFromConstError{});
                }),
            m_value);
    }
    ValueType const& get() const&
    {
        return std::visit(
            overloaded([](ValueType& ref) -> ValueType const& { return ref; },
                [](ValueType const& ref) -> ValueType const& { return ref; },
                [](std::reference_wrapper<ValueType> const& ref) -> ValueType const& {
                    return ref.get();
                },
                [](std::reference_wrapper<ValueType const> const& ref) -> ValueType const& {
                    return ref.get();
                }),
            m_value);
    }

    Value& operator()() & { return get(); }
    Value const& operator()() const& { return get(); }
};

}  // namespace bcos::utilities