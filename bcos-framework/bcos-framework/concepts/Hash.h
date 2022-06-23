#pragma once
#include "Basic.h"

namespace bcos::concepts::hash
{

template <class ObjectType>
concept Hashable = requires(ObjectType object)
{
    {
        hash(object)
        } -> ByteBuffer;
};

}  // namespace bcos::concepts::hash
