#pragma once

#include "Basic.h"
#include <bcos-utilities/Ranges.h>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <stdexcept>
#include <type_traits>

namespace bcos::concepts::bytebuffer
{

template <class ByteBufferType>
concept ByteBuffer = RANGES::contiguous_range<ByteBufferType> &&
    std::is_trivial_v<RANGES::range_value_t<std::remove_cvref_t<ByteBufferType>>> &&
    std::is_standard_layout_v<RANGES::range_value_t<std::remove_cvref_t<ByteBufferType>>> &&
    (sizeof(RANGES::range_value_t<std::remove_cvref_t<ByteBufferType>>) == 1);

template <class HashType>
concept Hash = ByteBuffer<HashType>;

std::string_view toView(ByteBuffer auto const& buffer)
{
    return std::string_view((const char*)RANGES::data(buffer), RANGES::size(buffer));
}

void assignTo(ByteBuffer auto const& from, ByteBuffer auto& to)
{
    resizeTo(to, RANGES::size(from));
    std::copy_n(reinterpret_cast<const std::byte*>(RANGES::data(from)), RANGES::size(from),
        reinterpret_cast<std::byte*>(RANGES::data(to)));
}

bool equalTo(ByteBuffer auto const& left, ByteBuffer auto const& right)
{
    if (RANGES::size(left) != RANGES::size(right))
    {
        return false;
    }

    return std::equal(reinterpret_cast<const std::byte*>(RANGES::data(left)),
        reinterpret_cast<const std::byte*>(RANGES::data(left)) + RANGES::size(left),
        reinterpret_cast<const std::byte*>(RANGES::data(right)));
}
}  // namespace bcos::concepts::bytebuffer