#pragma once
#include "Basic.h"
#include "ByteBuffer.h"
#include <vector>

namespace bcos::concepts::serialize
{

template <class ObjectType>
concept Serializable = requires(
    ObjectType object, std::vector<std::byte> out, std::vector<std::byte> in)
{
    impl_encode(object, out);
    impl_decode(in, object);
};

template <class ObjectType>
auto encode(ObjectType const& in, bcos::concepts::bytebuffer::ByteBuffer auto& out)
{
    impl_encode(in, out);
}

template <class ObjectType>
void decode(bcos::concepts::bytebuffer::ByteBuffer auto const& in, ObjectType& out)
{
    impl_decode(in, out);
}

}  // namespace bcos::concepts::serialize