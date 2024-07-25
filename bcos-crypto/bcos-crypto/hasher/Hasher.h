#pragma once
#include <bcos-utilities/Ranges.h>
#include <concepts>
#include <span>
#include <type_traits>

namespace bcos::crypto::hasher
{

template <class HasherType>
concept Hasher = requires(HasherType hasher, std::span<std::byte> out) {
    //    requires std::move_constructible<HasherType>;
    hasher.update(std::span<std::byte const>{});
    hasher.final(out);
    requires std::same_as<
        std::decay_t<std::invoke_result_t<decltype(&std::decay_t<HasherType>::clone), HasherType>>,
        std::decay_t<HasherType>>;
};

}  // namespace bcos::crypto::hasher
