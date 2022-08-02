#pragma once
#include <bcos-utilities/Ranges.h>
#include <boost/throw_exception.hpp>

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

std::string_view toView(ByteBuffer auto const& buffer)
{
    return std::string_view((const char*)RANGES::data(buffer), RANGES::size(buffer));
}

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
concept DynamicRange = requires(Range range, size_t newSize)
{
    RANGES::range<Range>;
    range.resize(newSize);
    range.reserve(newSize);
};

void resizeTo(RANGES::range auto& out, size_t size)
{
    if (RANGES::size(out) < size)
    {
        if constexpr (bcos::concepts::DynamicRange<std::remove_cvref_t<decltype(out)>>)
        {
            out.resize(size);
            return;
        }
        BOOST_THROW_EXCEPTION(std::runtime_error{"Not enough output space!"});
    }
}

}  // namespace bcos::concepts