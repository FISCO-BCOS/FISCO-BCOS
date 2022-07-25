#pragma once
#include "Basic.h"
#include <vector>

namespace bcos::concepts::serialize
{

template <class ObjectType>
concept Serializable = requires(ObjectType object)
{
    impl_encode(object, std::vector<std::byte>{});
    impl_decode(std::vector<std::byte>{}, object);
};

template <class ObjectType>
auto encode(ObjectType const& in, bcos::concepts::ByteBuffer auto& out)
{
    impl_encode(in, out);
}

template <class ObjectType>
void decode(bcos::concepts::ByteBuffer auto const& in, ObjectType& out)
{
    impl_decode(in, out);
}

}  // namespace bcos::concepts::serialize