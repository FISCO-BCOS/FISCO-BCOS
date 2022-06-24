#pragma once
#include "Basic.h"
#include <bcos-crypto/hasher/Hasher.h>

namespace bcos::concepts::hash
{

template <class ObjectType, class Hasher>
concept Hashable = requires(ObjectType object)
{
    bcos::crypto::hasher::Hasher<Hasher>;
    {
        calculate<Hasher>(object)
        } -> ByteBuffer;
};

}  // namespace bcos::concepts::hash
