#pragma once

#include <ranges>

namespace bcos::concepts
{

template <class ByteBufferType>
concept ByteBuffer = std::ranges::range<ByteBufferType> &&
    (sizeof(std::ranges::range_value_t<std::remove_cvref_t<ByteBufferType>>) == 1);

template <class HashType>
concept Hash = ByteBuffer<HashType>;

template <class Pointer>
concept PointerLike = requires(Pointer p)
{
    *p;
    p.operator->();
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

}  // namespace bcos::concepts