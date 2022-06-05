#pragma once
#include "../Concepts.h"
#include <boost/throw_exception.hpp>
#include <iterator>
#include <ranges>
#include <span>
#include <type_traits>

namespace bcos::crypto::hasher
{

template <class HasherType>
concept Hasher = requires(HasherType& hasher, std::span<std::byte> buffer)
{
    HasherType{};
    update(hasher, buffer);
    final(hasher, buffer);
};

}  // namespace bcos::crypto::hasher