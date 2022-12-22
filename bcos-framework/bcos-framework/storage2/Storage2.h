#pragma once

#include "Exception.h"
#include "Iterator.h"
#include <bcos-concepts/Basic.h>
#include <bcos-concepts/ByteBuffer.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Ranges.h>
#include <boost/throw_exception.hpp>
#include <range/v3/view/transform.hpp>
#include <type_traits>

namespace bcos::storage2
{
using OptionalEntry = std::optional<bcos::storage::Entry>;

template <class KeysType>
concept InputKeys = RANGES::input_range<KeysType> &&
    concepts::bytebuffer::ByteBuffer<RANGES::range_value_t<KeysType>>;
template <class EntriesType>
concept InputEntries = RANGES::range<EntriesType> &&
    std::same_as<RANGES::range_value_t<EntriesType>, bcos::storage::Entry>;
template <class EntriesType>
concept OutputEntries = RANGES::output_range<EntriesType, OptionalEntry>;

// The class Impl only need impl_getRows and impl_setRows
template <class Impl, class Key, class Value>
class Storage2Base
{
public:
    static constexpr std::string_view SYS_TABLES{"s_tables"};

    using SelectOperation = std::tuple<Key>;
    using InsertOperation = std::tuple<Key>;
    using UpdateOperation = std::tuple<Key, Value>;
    using DeleteOperation = std::tuple<Key>;
    using Operation = std::variant<InsertOperation, UpdateOperation, DeleteOperation>;

    using SelectResult = std::tuple<Value>;

    // BEGIN: Pure interfaces
    task::Task<void> read(RANGES::range auto const& keys) requires
        std::constructible_from<RANGES::range_value_t<decltype(keys)>, Key>
    {
        return impl().impl_read(keys);
    }


    task::Task<void> write(RANGES::range auto operations)
    {
        return impl().impl_write(tableName, keys, entries, type);
    }

    template <Iterator IteratorType>
    task::Task<IteratorType> seek(std::string_view tableName, std::string_view key)
    {
        return impl().impl_seek(tableName, key);
    }
    // END: Pure interfaces

    task::Task<OptionalEntry> getRow(std::string_view tableName, std::string_view key)
    {
        RANGES::single_view<decltype(key)> keysView{key};

        OptionalEntry entry;
        auto valuesView =
            RANGES::single_view<OptionalEntry*>(&entry) |
            RANGES::views::transform([](OptionalEntry* ptr) -> OptionalEntry& { return *ptr; });

        co_await impl().impl_getRows(tableName, keysView, valuesView);
        co_return entry;
    }

    task::Task<void> setRow(
        std::string_view tableName, std::string_view key, bcos::storage::Entry entry)
    {
        RANGES::single_view<decltype(key)> keysView{key};
        auto valuesView =
            RANGES::single_view<bcos::storage::Entry*>(&entry) |
            RANGES::views::transform(
                [](bcos::storage::Entry* ptr) -> bcos::storage::Entry& { return *ptr; });

        co_await impl().impl_setRows(tableName, keysView, valuesView);
    }

    task::Task<void> createTable(std::string_view tableName)
    {
        auto entry = co_await getRow(SYS_TABLES, tableName);
        if (entry)
        {
            BOOST_THROW_EXCEPTION(TableExists{});
        }

        storage::Entry tableEntry;
        // TODO: Write value fields
        // ...
        co_await setRow(SYS_TABLES, tableName, std::move(tableEntry));
    }

private:
    friend Impl;
    auto& impl() { return static_cast<Impl&>(*this); }

    Storage2Base() = default;
};

template <class Impl, class Entry>
concept Storage = std::derived_from<Impl, Storage2Base<Impl, Entry>>;

}  // namespace bcos::storage2