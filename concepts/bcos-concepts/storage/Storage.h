#pragma once

#include "../Basic.h"
#include "../ByteBuffer.h"
#include <bcos-framework/storage/Entry.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Ranges.h>
#include <type_traits>

namespace bcos::concepts::storage
{

template <class KeyType>
concept KeyLike = bytebuffer::ByteBuffer<KeyType>;
template <class KeysType>
concept Keys = RANGES::range<KeysType> && bytebuffer::ByteBuffer<RANGES::range_value_t<KeysType>>;

template <class EntryType>
concept EntryLike = std::same_as<EntryType, bcos::storage::Entry> || PointerLike<EntryType>;
template <class KeysType>
concept Entries = RANGES::range<KeysType> && EntryLike<RANGES::range_value_t<KeysType>>;

template <class Impl>
class StorageBase
{
public:
    task::Task<void> getRow(
        bytebuffer::ByteBuffer auto const& tableName, KeyLike auto const& key, EntryLike auto& out)
    {
        RANGES::single_view<decltype(key)> keys{bytebuffer::toView(key)};
        RANGES::single_view<decltype(key)> entries(&out);

        co_await impl().impl_getRows(tableName, keys, entries);
    }

    task::Task<void> setRow(bytebuffer::ByteBuffer auto const& tableName, KeyLike auto const& key,
        EntryLike auto const& entry)
    {
        RANGES::single_view<decltype(key)> keys{bytebuffer::toView(key)};
        RANGES::single_view<decltype(key)> entries(&entry);

        co_await impl().impl_setRow(tableName, keys, entries);
    }

    task::Task<void> getRows(
        bytebuffer::ByteBuffer auto const& tableName, Keys auto const& keys, Entries auto& out)
    {
        co_await impl().impl_getRows(tableName, keys, out);
    }

    task::Task<void> setRows(bytebuffer::ByteBuffer auto const& tableName, Keys auto const& keys,
        Entries auto const& entries)
    {
        co_await impl().impl_setRows(tableName, keys, entries);
    }

    task::Task<void> createTable(bytebuffer::ByteBuffer auto const& tableName)
    {
        co_await impl().impl_createTable(tableName);
    }

private:
    friend Impl;
    StorageBase() = default;
    auto& impl() { return static_cast<Impl&>(*this); }
};

template <class Impl>
concept Storage = std::derived_from<Impl, StorageBase<Impl>>;

}  // namespace bcos::concepts::storage