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
concept HasMemberFuncEncode = requires(Object object, Buffer& output, const Buffer& input)
{
    object.encode(output);
};

template <class Object, class Buffer>
concept HasMemberFuncDecode = requires(Object object, Buffer& output, const Buffer& input)
{
    object.decode(input);
};

template <class Object, class Buffer>
concept HasADLEncode = requires(Object object, Buffer& output, const Buffer& input)
{
    impl_encode(object, output);
};

template <class Object, class Buffer>
concept HasADLDecode = requires(Object object, Buffer& output, const Buffer& input)
{
    impl_decode(input, object);
};

struct encode
{
    void operator()(auto&& object, bcos::concepts::bytebuffer::ByteBuffer auto& out) const
    {
        using ObjectType = std::remove_cvref_t<decltype(object)>;
        using BufferType = std::remove_cvref_t<decltype(out)>;
        if constexpr (HasMemberFuncEncode<ObjectType, BufferType>)
        {
            object.encode(out);
        }
        else if constexpr (HasADLEncode<ObjectType, BufferType>)
        {
            impl_encode(object, out);
        }
        else
        {
            static_assert(sizeof(ObjectType*), "Not found member function or adl!");
        }
    }
};

struct decode
{
    void operator()(bcos::concepts::bytebuffer::ByteBuffer auto const& input, auto& object) const
    {
        using BufferType = std::remove_cvref_t<decltype(input)>;
        using ObjectType = std::remove_cvref_t<decltype(object)>;

        if constexpr (HasMemberFuncDecode<ObjectType, BufferType>)
        {
            object.decode(input);
        }
        else if constexpr (HasADLDecode<ObjectType, BufferType>)
        {
            impl_decode(input, object);
        }
        else
        {
            static_assert(!sizeof(ObjectType*), "Not found member function or adl!");
        }
    }
};
}  // namespace detail

constexpr inline detail::encode encode{};
constexpr inline detail::decode decode{};

template <class Object>
concept Serializable = true;

}  // namespace bcos::concepts::serialize