#pragma once
#include "Basic.h"
#include <bcos-crypto/hasher/Hasher.h>
#include <boost/type_traits.hpp>
#include <array>

namespace bcos::concepts::hash
{

namespace detail
{
template <class ObjectType, class Hasher>
concept HasADL = requires(ObjectType object, std::string out1, std::vector<char> out2,
    std::vector<unsigned char> out3, std::vector<std::byte> out4)
{
    bcos::crypto::hasher::Hasher<Hasher>;
    impl_calculate<Hasher>(object, out1);
    impl_calculate<Hasher>(object, out2);
    impl_calculate<Hasher>(object, out3);
    impl_calculate<Hasher>(object, out4);
};

template <bcos::crypto::hasher::Hasher Hasher>
struct calculate
{
    void operator()(auto const& object, ByteBuffer auto& out) const
    {
        using ObjectType = std::remove_cvref_t<decltype(object)>;

        if constexpr (HasADL<ObjectType, Hasher>)
        {
            return impl_calculate<Hasher>(object, out);
        }
        else
        {
            static_assert(!sizeof(object), "Not found adl!");
        }
    }
};
}  // namespace detail

template <bcos::crypto::hasher::Hasher Hasher>
constexpr inline detail::calculate<Hasher> calculate{};

template <class Object>
concept Hashable = true;

}  // namespace bcos::concepts::hash
