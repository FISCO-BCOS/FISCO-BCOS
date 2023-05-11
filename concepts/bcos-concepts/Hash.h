#pragma once
#include "Basic.h"
#include "ByteBuffer.h"
#include <bcos-crypto/hasher/Hasher.h>
#include <boost/type_traits.hpp>
#include <array>
#include <range/v3/range/concepts.hpp>

namespace bcos::concepts::hash
{

namespace detail
{
template <class ObjectType>
concept HasMemberFunc = requires(ObjectType object) { object.hash(); };

template <class ObjectType, class Hasher>
concept HasADL = requires(Hasher&& hasher, ObjectType object, std::string out1,
    std::vector<char> out2, std::vector<unsigned char> out3, std::vector<std::byte> out4) {
                     requires bcos::crypto::hasher::Hasher<Hasher>;
                     impl_calculate(std::forward<Hasher>(hasher), object, out1);
                     impl_calculate(std::forward<Hasher>(hasher), object, out2);
                     impl_calculate(std::forward<Hasher>(hasher), object, out3);
                     impl_calculate(std::forward<Hasher>(hasher), object, out4);
                 };

struct calculate
{
    void operator()(auto&& hasher, auto const& object, ByteBuffer auto& out) const
    {
        using ObjectType = std::remove_cvref_t<decltype(object)>;

        if constexpr (HasMemberFunc<ObjectType>)
        {
            auto hash = object.hash();
            bytebuffer::assignTo(hash, out);
        }
        else if constexpr (HasADL<ObjectType, std::remove_cvref_t<decltype(hasher)>>)
        {
            impl_calculate(std::forward<decltype(hasher)>(hasher), object, out);
        }
        else
        {
            static_assert(!sizeof(object), "Not found member function or adl!");
        }
    }
};
}  // namespace detail

constexpr inline detail::calculate calculate{};

}  // namespace bcos::concepts::hash
