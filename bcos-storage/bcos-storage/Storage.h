#pragma once

#include <bcos-framework/storage/Entry.h>
#include <ranges>
#include <type_traits>

namespace bcos::storage
{

template <class StorageType>
concept Storage = requires(StorageType storage)
{
    {
        storage.getRow(std::string_view{}, std::string_view{})
        } -> std::convertible_to<std::optional<Entry>>;
    {
        storage.getRows(std::string_view{}, std::vector<std::string_view>{})
        } -> std::ranges::range;
    storage.setRow(std::string_view{}, std::string_view{}, Entry{});
    storage.createTable(std::string{});
};

}  // namespace bcos::storage