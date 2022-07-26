#pragma once
#include <bcos-utilities/Ranges.h>

namespace bcos::concepts
{

template <class ByteBufferType>
concept ByteBuffer = RANGES::range<ByteBufferType> &&
    (sizeof(RANGES::range_value_t<std::remove_cvref_t<ByteBufferType>>) == 1);

template <class HashType>
concept Hash = ByteBuffer<HashType>;

template <class Pointer>
concept PointerLike = requires(Pointer p)
{
    *p;
    p.operator->();
};

template <class Range>
concept DynamicRange = requires(Range range, size_t newSize)
{
    RANGES::range<Range>;
    range.resize(newSize);
    range.reserve(newSize);
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