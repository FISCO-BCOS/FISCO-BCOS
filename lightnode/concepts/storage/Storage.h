#pragma once

#include "../Basic.h"
#include <bcos-framework/storage/Entry.h>
#include <ranges>
#include <type_traits>

namespace bcos::concepts::storage
{

template <class KeyType>
concept Key = bcos::concepts::ByteBuffer<KeyType> ||
    (std::ranges::range<KeyType>&& bcos::concepts::ByteBuffer<std::ranges::range_value_t<KeyType>>);

template <class ValueType>
concept Value = Key<ValueType>;

template <class Impl>
class StorageBase
{
public:
    auto getRows(std::ranges::range auto const& keys) { impl().impl_getRows(keys); }

    void setRows(std::string_view table, std::string_view key, bcos::storage::Entry entry)
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