#pragma once
#include "Basic.h"
#include <vector>

namespace bcos::concepts::serialize
{

template <class ObjectType>
concept Serializable = requires(ObjectType object)
{
    {
        impl_encode(object)
        } -> ByteBuffer;
    impl_decode(object, std::vector<std::byte>{});
};

template <class ObjectType>
auto encode(ObjectType const& object)
{
    return impl_encode(object);
}

template <class ObjectType>
void decode(ObjectType& object, bcos::concepts::ByteBuffer auto const& byteBuffer)
{
    impl_decode(object, byteBuffer);
}

}  // namespace bcos::concepts::serialize