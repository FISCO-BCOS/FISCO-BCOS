#pragma once
#include <bcos-utilities/Ranges.h>
#include <concepts>
#include <span>
#include <type_traits>

namespace bcos::crypto::hasher
{

template <class HasherType>
concept Hasher = requires(HasherType hasher, std::span<std::byte> out) {
                     requires std::move_constructible<HasherType>;
                     hasher.update(std::span<std::byte const>{});
                     hasher.final(out);
                     {
                         hasher.clone()
                         } -> std::same_as<HasherType>;
                 };

}  // namespace bcos::crypto::hasher