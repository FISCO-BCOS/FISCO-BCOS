#pragma once
#include "../Concepts.h"
#include <concepts>
#include <ranges>
#include <span>
#include <type_traits>

namespace bcos::crypto::hasher
{

template <class HasherType>
concept Hasher = requires(HasherType hasher)
{
    HasherType{};
    HasherType::HASH_SIZE->std::integral;
    hasher.update(std::span<std::byte>{});
    hasher.update(std::span<std::byte const>{});
    hasher.final(std::span<std::byte>{});
};

auto final(Hasher auto& hasher)
{
    std::array<std::byte, std::remove_cvref_t<decltype(hasher)>::HASH_SIZE> out;
    hasher.final(out);
    return out;
}

}  // namespace bcos::crypto::hasher