#pragma once

#include "Basic.h"
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <ranges>

namespace bcos::concepts::bytebuffer
{

template <class ByteBufferType>
concept ByteBuffer =
    std::ranges::contiguous_range<ByteBufferType> &&
    std::is_trivial_v<std::ranges::range_value_t<std::remove_cvref_t<ByteBufferType>>> &&
    std::is_standard_layout_v<std::ranges::range_value_t<std::remove_cvref_t<ByteBufferType>>> &&
    (sizeof(std::ranges::range_value_t<std::remove_cvref_t<ByteBufferType>>) == 1);

template <class HashType>
concept Hash = ByteBuffer<HashType>;

std::string_view toView(ByteBuffer auto const& buffer)
{
    return std::string_view((const char*)std::ranges::data(buffer), std::ranges::size(buffer));
}

void assignTo(ByteBuffer auto const& from, ByteBuffer auto& to)
{
    resizeTo(to, std::ranges::size(from));
    std::copy_n(reinterpret_cast<const std::byte*>(std::ranges::data(from)),
        std::ranges::size(from), reinterpret_cast<std::byte*>(std::ranges::data(to)));
}

bool equalTo(ByteBuffer auto const& left, ByteBuffer auto const& right)
{
    if (std::ranges::size(left) != std::ranges::size(right))
    {
        return false;
    }

    return std::equal(reinterpret_cast<const std::byte*>(std::ranges::data(left)),
        reinterpret_cast<const std::byte*>(std::ranges::data(left)) + std::ranges::size(left),
        reinterpret_cast<const std::byte*>(std::ranges::data(right)));
}
}  // namespace bcos::concepts::bytebuffer