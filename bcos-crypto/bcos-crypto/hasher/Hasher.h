#pragma once
#include <bcos-utilities/Ranges.h>
#include <concepts>
#include <span>
#include <type_traits>

namespace bcos::crypto::hasher
{

template <class HasherType>
concept Hasher = requires(HasherType hasher, std::span<std::byte> out)
{
    HasherType{};
    HasherType::HASH_SIZE > 0;
    hasher.update(std::span<std::byte const>{});
    hasher.final(out);
};

auto final(Hasher auto& hasher)
{
    std::array<std::byte, std::remove_cvref_t<decltype(hasher)>::HASH_SIZE> out;
    hasher.final(out);
    return out;
}

}  // namespace bcos::crypto::hasher