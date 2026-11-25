#pragma once
#include <boost/throw_exception.hpp>
#include <range/v3/range.hpp>
#include <ranges>
#include <stdexcept>
#include <type_traits>

namespace bcos::concepts
{

template <class ByteBufferType>
concept ByteBuffer =
    std::ranges::contiguous_range<ByteBufferType> &&
    std::is_trivial_v<std::ranges::range_value_t<std::remove_cvref_t<ByteBufferType>>> &&
    std::is_standard_layout_v<std::ranges::range_value_t<std::remove_cvref_t<ByteBufferType>>> &&
    (sizeof(std::ranges::range_value_t<std::remove_cvref_t<ByteBufferType>>) == 1);

template <class T>
concept StringLike = std::same_as<std::remove_cvref_t<T>, std::string> ||
                     std::same_as<std::remove_cvref_t<T>, std::string_view>;

template <class Pointer>
concept PointerLike = requires(Pointer pointer) {
    *pointer;
    pointer.operator->();
};

template <class Input>
auto& getRef(Input& input)
{
    if constexpr (PointerLike<Input>)
    {
        return *input;
    }
    else
    {
        return input;
    }
}

template <class Range>
concept DynamicRange = requires(Range range, size_t newSize) {
    requires std::ranges::range<Range>;
    range.resize(newSize);
    range.reserve(newSize);
};

void resizeTo(std::ranges::range auto& out, std::integral auto size)
{
    if ((size_t)std::ranges::size(out) < (size_t)size)
    {
        if constexpr (bcos::concepts::DynamicRange<std::remove_cvref_t<decltype(out)>>)
        {
            out.resize(size);
            return;
        }
        BOOST_THROW_EXCEPTION(std::runtime_error{"Not enough space"});
    }
}
}  // namespace bcos::concepts
