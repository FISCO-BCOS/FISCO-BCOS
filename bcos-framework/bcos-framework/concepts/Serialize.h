#pragma once
#include "Basic.h"

namespace bcos::concepts::serialize
{

template <class ObjectType>
concept Serializable = requires(ObjectType object) {
                           {
                               encode(object)
                               } -> ByteBuffer;
                           decode(object, std::vector<std::byte>{});
                       };
}  // namespace bcos::concepts::serialize