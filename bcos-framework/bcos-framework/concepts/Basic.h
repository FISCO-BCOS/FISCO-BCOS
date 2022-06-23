#pragma once
#include <boost/type_traits.hpp>
#include <boost/type_traits/function_traits.hpp>
#include <ranges>

namespace bcos::concepts
{

template <class ByteBufferType>
concept ByteBuffer = std::ranges::range<ByteBufferType> &&(sizeof(std::ranges::range_value_t<ByteBufferType>) == 1);

template <class HashType>
concept Hash = ByteBuffer<HashType>;

}  // namespace bcos::concepts