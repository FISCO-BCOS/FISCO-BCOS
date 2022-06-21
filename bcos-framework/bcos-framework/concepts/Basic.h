#pragma once
#include <boost/type_traits.hpp>
#include <boost/type_traits/function_traits.hpp>
#include <ranges>

namespace bcos::concepts
{

template <class ByteBufferType>
concept ByteBuffer = std::ranges::range<ByteBufferType> &&(sizeof(std::ranges::range_value_t<ByteBufferType>) == 1);

template <class ObjectType>
concept Serializable = requires(
    ObjectType object, typename boost::function_traits<decltype(&ObjectType::decode)>::arg1_type decodeArg1)
{
    {
        object.encode()
        } -> ByteBuffer;
    {
        decodeArg1
        } -> ByteBuffer;
    {object.decode(decodeArg1)};
};
};  // namespace bcos::concepts