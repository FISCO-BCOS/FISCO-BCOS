#pragma once

#include <bcos-framework/storage/Entry.h>
#include <ranges>
#include <type_traits>

namespace bcos::concepts::storage
{

template <class StorageType>
concept Storage = requires(StorageType storage)
{
    // clang-format off
    { storage.getRow(std::string_view{}, std::string_view{}) } -> std::convertible_to<std::optional<bcos::storage::Entry>>;
    { storage.getRows(std::string_view{}, std::vector<std::string_view>{}) } -> std::ranges::range;
    // clang-format on
    storage.setRow(std::string_view{}, std::string_view{}, bcos::storage::Entry{});
    storage.createTable(std::string{});
};

}  // namespace bcos::concepts::storage