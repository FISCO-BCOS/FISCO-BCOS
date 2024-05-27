#pragma once
#include "Basic.h"
#include <bcos-crypto/hasher/Hasher.h>
#include <boost/type_traits.hpp>

namespace bcos::concepts::hash
{

constexpr inline struct Calculate
{
    void operator()(auto&& object, auto&& hasher, ByteBuffer auto& output) const
    {
        tag_invoke(*this, std::forward<decltype(object)>(object),
            std::forward<decltype(hasher)>(hasher), output);
    }
} calculate{};

template <auto& Tag>
using tag_t = std::decay_t<decltype(Tag)>;

}  // namespace bcos::concepts::hash
