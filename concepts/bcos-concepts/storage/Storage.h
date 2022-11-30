#pragma once

#include "../Basic.h"
#include <bcos-framework/storage/Entry.h>
#include <bcos-utilities/Ranges.h>
#include <type_traits>

namespace bcos::concepts::storage
{

template <class Impl>
class StorageBase
{
public:
    std::optional<bcos::storage::Entry> getRow(std::string_view table, std::string_view key)
    {
        return impl().impl_getRow(table, key);
    }

    std::vector<std::optional<bcos::storage::Entry>> getRows(
        std::string_view table, RANGES::range auto&& keys)
    {
        return impl().impl_getRows(table, std::forward<decltype(keys)>(keys));
    }

    void setRow(std::string_view table, std::string_view key, bcos::storage::Entry entry)
    {
        impl().impl_setRow(table, key, std::move(entry));
    }

    void createTable(std::string tableName) { impl().impl_createTable(std::move(tableName)); }

private:
    friend Impl;
    StorageBase() = default;
    auto& impl() { return static_cast<Impl&>(*this); }
};

template <class Impl>
concept Storage = std::derived_from<Impl, StorageBase<Impl>>;

}  // namespace bcos::concepts::storage