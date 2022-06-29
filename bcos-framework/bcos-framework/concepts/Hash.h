#pragma once
#include "Basic.h"
#include <bcos-crypto/hasher/Hasher.h>
#include <boost/type_traits.hpp>
#include <array>

namespace bcos::concepts::hash
{

template <class ObjectType>
concept Hashable = requires(ObjectType object)
{
    {
        impl_calculate(object)
        } -> ByteBuffer;
};

template <bcos::crypto::hasher::Hasher Hasher>
auto calculate(auto const& obj)
{
    return impl_calculate<Hasher>(obj);
}
}  // namespace bcos::concepts::hash
