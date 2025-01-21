#pragma once

#include "Basic.h"
#include <boost/throw_exception.hpp>
#include <algorithm>

namespace bcos::concepts::bytebuffer
{

template <class ByteBufferType>
concept ByteBuffer =
    ::ranges::contiguous_range<ByteBufferType> &&
    std::is_trivial_v<::ranges::range_value_t<std::remove_cvref_t<ByteBufferType>>> &&
    std::is_standard_layout_v<::ranges::range_value_t<std::remove_cvref_t<ByteBufferType>>> &&
    (sizeof(::ranges::range_value_t<std::remove_cvref_t<ByteBufferType>>) == 1);

template <class HashType>
concept Hash = ByteBuffer<HashType>;

std::string_view toView(ByteBuffer auto const& buffer)
{
    return std::string_view((const char*)::ranges::data(buffer), ::ranges::size(buffer));
}

void assignTo(ByteBuffer auto const& from, ByteBuffer auto& to)
{
    resizeTo(to, ::ranges::size(from));
    std::copy_n(reinterpret_cast<const std::byte*>(::ranges::data(from)), ::ranges::size(from),
        reinterpret_cast<std::byte*>(::ranges::data(to)));
}

bool equalTo(ByteBuffer auto const& left, ByteBuffer auto const& right)
{
    if (::ranges::size(left) != ::ranges::size(right))
    {
        return false;
    }

    return std::equal(reinterpret_cast<const std::byte*>(::ranges::data(left)),
        reinterpret_cast<const std::byte*>(::ranges::data(left)) + ::ranges::size(left),
        reinterpret_cast<const std::byte*>(::ranges::data(right)));
}
}  // namespace bcos::concepts::bytebuffer