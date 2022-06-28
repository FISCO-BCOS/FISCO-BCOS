#pragma once
#include "Basic.h"
#include <bcos-crypto/hasher/Hasher.h>
#include <boost/type_traits.hpp>

namespace bcos::concepts::hash
{

template <class ObjectType>
concept Hashable = requires(ObjectType object) {
                       {
                           calculate(object)
                           } -> ByteBuffer;
                   };

}  // namespace bcos::concepts::hash
