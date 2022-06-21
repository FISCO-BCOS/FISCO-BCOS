#pragma once
#include <ranges>

namespace bcos::concepts
{

template <class ByteBufferType>
concept ByteBuffer = std::ranges::range<ByteBufferType> &&(sizeof(std::ranges::range_value_t<ByteBufferType>) == 1);

};