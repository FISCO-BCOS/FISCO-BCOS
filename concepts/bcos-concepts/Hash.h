#pragma once
#include "Basic.h"
#include <bcos-crypto/hasher/Hasher.h>
#include <boost/type_traits.hpp>
#include <array>

namespace bcos::concepts::hash
{

template <class ObjectType>
concept Hashable = requires(ObjectType object, std::string out1, std::vector<char> out2,
    std::vector<unsigned char> out3, std::vector<std::byte> out4)
{
    impl_calculate(object, out1);
    impl_calculate(object, out2);
    impl_calculate(object, out3);
    impl_calculate(object, out4);
};

template <bcos::crypto::hasher::Hasher Hasher>
auto calculate(auto const& obj, ByteBuffer auto& out)
{
    return impl_calculate<Hasher>(obj, out);
}
}  // namespace bcos::concepts::hash
