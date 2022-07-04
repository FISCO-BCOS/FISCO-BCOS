#pragma once
#include <ranges>

namespace bcos::concepts
{

template <class ByteBufferType>
concept ByteBuffer = std::ranges::range<ByteBufferType> &&
    (sizeof(std::ranges::range_value_t<std::remove_cvref_t<ByteBufferType>>) == 1);

template <class HashType>
concept Hash = ByteBuffer<HashType>;

template <class Pointer, class Type>
concept PointerLike = requires(Pointer p)
{
    std::is_pointer_v<Type>;
    // clang-format off
    { *p } -> std::same_as<Type&>;
    // clang-format on
};

}  // namespace bcos::concepts