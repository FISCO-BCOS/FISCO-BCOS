#pragma once
#include "Basic.h"
#include "ByteBuffer.h"
#include <boost/endian.hpp>
#include <boost/endian/conversion.hpp>
#include <vector>

namespace bcos::concepts::serialize
{

namespace detail
{

template <class Object, class Buffer>
concept HasMemberFunc = requires(Object object, Buffer& output, const Buffer& input)
{
    object.encode(output);
    object.decode(input);
};

template <class Object, class Buffer>
concept HasADL = requires(Object object, Buffer& output, const Buffer& input)
{
    impl_encode(object, output);
    impl_decode(input, object);
};

struct encode
{
    void operator()(auto&& object, bcos::concepts::bytebuffer::ByteBuffer auto& out) const
    {
        using ObjectType = std::remove_cvref_t<decltype(object)>;
        using BufferType = std::remove_cvref_t<decltype(out)>;
        if constexpr (HasMemberFunc<ObjectType, BufferType>)
        {
            object.encode(out);
        }
        else if constexpr (HasADL<ObjectType, BufferType>)
        {
            impl_encode(object, out);
        }
    }
};

struct decode
{
    void operator()(bcos::concepts::bytebuffer::ByteBuffer auto const& input, auto&& object) const
    {
        using ObjectType = std::remove_cvref_t<decltype(object)>;
        using BufferType = std::remove_cvref_t<decltype(input)>;
        
        if constexpr (HasMemberFunc<ObjectType, BufferType>)
        {
            object.decode(input);
        }
        else if constexpr (HasADL<ObjectType, BufferType>)
        {
            impl_decode(input, object);
        }
        else
        {
            static_assert(!sizeof(object), "Not found member function or adl!");
        }
    }
};
}  // namespace detail

constexpr inline detail::encode encode{};
constexpr inline detail::decode decode{};

template <class Object>
concept Serializable = requires(
    Object object, std::vector<char> output, const std::vector<char>& input)
{
    encode(object, output);
    decode(input, object);
};

}  // namespace bcos::concepts::serialize