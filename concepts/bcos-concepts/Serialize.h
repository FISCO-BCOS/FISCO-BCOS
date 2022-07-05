#pragma once
#include "Basic.h"
#include <vector>

namespace bcos::concepts::serialize
{

template <class ObjectType>
concept Serializable = requires(ObjectType object)
{
    impl_encode(object, std::vector<std::byte>{});
    impl_decode(object, std::vector<std::byte>{});
};

template <class ObjectType>
auto encode(ObjectType const& object, bcos::concepts::ByteBuffer auto& out)
{
    impl_encode(object, out);
}

template <class ObjectType>
void decode(ObjectType& object, bcos::concepts::ByteBuffer auto const& in)
{
    impl_decode(object, in);
}

}  // namespace bcos::concepts::serialize