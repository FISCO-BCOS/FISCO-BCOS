#pragma once

#include "../Basic.h"
#include "../ByteBuffer.h"
#include <bcos-framework/storage/Entry.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Ranges.h>
#include <type_traits>

namespace bcos::concepts::storage
{

template <class TableNameType>
concept TableName = bytebuffer::ByteBuffer<TableNameType>;

template <class KeyType>
concept Key = bytebuffer::ByteBuffer<KeyType>;

template <class KeysType>
concept Keys = RANGES::range<KeysType> && bytebuffer::ByteBuffer<RANGES::range_value_t<KeysType>>;

using OptionalEntry = std::optional<bcos::storage::Entry>;

template <class EntriesType>
concept OptionalEntries =
    RANGES::range<EntriesType> && std::same_as<RANGES::range_value_t<EntriesType>, OptionalEntry>;

template <class EntriesType>
concept Entries = RANGES::range<EntriesType> &&
    std::same_as<RANGES::range_value_t<EntriesType>, bcos::storage::Entry>;

// The class Impl only need impl_getRows and impl_setRows
template <class Impl>
class StorageBase
{
public:
    task::Task<OptionalEntry> getRow(TableName auto const& tableName, Key auto const& key)
    {
        RANGES::single_view<decltype(key)> keys{bytebuffer::toView(key)};

        OptionalEntry entry;
        RANGES::single_view<decltype(key)> entries(entry);

        co_await impl().impl_getRows(tableName, keys, entries);
        co_return entry;
    }

    task::Task<void> setRow(
        TableName auto const& tableName, Key auto const& key, bcos::storage::Entry entry)
    {
        RANGES::single_view<decltype(key)> keys{bytebuffer::toView(key)};
        RANGES::single_view<decltype(key)> entries(&entry);

        co_await impl().impl_setRows(tableName, keys, entries);
    }

    task::Task<void> getRows(
        TableName auto const& tableName, Keys auto const& keys, OptionalEntries auto& out)
    {
        co_await impl().impl_getRows(tableName, keys, out);
    }

    task::Task<void> setRows(
        TableName auto const& tableName, Keys auto const& keys, Entries auto const& entries)
    {
        co_await impl().impl_setRows(tableName, keys, entries);
    }

    task::Task<void> createTable(TableName auto const& tableName)
    {
        // Impl it
    }

private:
    friend Impl;
    StorageBase() = default;
    auto& impl() { return static_cast<Impl&>(*this); }
};

template <class Impl>
concept Storage = std::derived_from<Impl, StorageBase<Impl>>;

}  // namespace bcos::concepts::storage