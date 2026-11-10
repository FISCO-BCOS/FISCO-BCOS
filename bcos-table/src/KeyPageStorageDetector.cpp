/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "KeyPageStorageDetector.h"
#include "KeyPageStorage.h"
#include <bcos-framework/storage/Serialize.h>
#include <boost/throw_exception.hpp>
#include <future>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bcos::storage
{
namespace
{
template <typename T>
bool hasExactEncoding(T const& output, std::string_view value)
{
    try
    {
        return serialize::encode(output) == value;
    }
    catch (std::exception const&)
    {
        return false;
    }
}

std::optional<KeyPageStorage::TableMeta> decodeArchive(std::string_view value)
{
    if (value.empty())
    {
        return std::nullopt;
    }
    try
    {
        KeyPageStorage::TableMeta output(value);
        if (!hasExactEncoding(output, value))
        {
            return std::nullopt;
        }
        return output;
    }
    catch (std::exception const&)
    {
        return std::nullopt;
    }
}

std::optional<KeyPageStorage::Page> decodeArchive(std::string_view value, std::string_view pageKey)
{
    if (value.empty())
    {
        return std::nullopt;
    }
    try
    {
        return KeyPageStorage::Page(value, pageKey);
    }
    catch (std::exception const&)
    {
        return std::nullopt;
    }
}

[[noreturn]] void throwReadError(std::string const& action, Error const& error)
{
    BOOST_THROW_EXCEPTION(BCOS_ERROR(StorageError::ReadError,
        "Failed to " + action + " while detecting KeyPage metadata: " + error.errorMessage()));
}

std::vector<std::string> getRegisteredTables(StorageInterface& storage)
{
    std::promise<std::pair<Error::UniquePtr, std::vector<std::string>>> promise;
    storage.asyncGetPrimaryKeys(StorageInterface::SYS_TABLES, std::nullopt,
        [&promise](Error::UniquePtr error, std::vector<std::string> tables) {
            promise.set_value({std::move(error), std::move(tables)});
        });
    auto [error, tables] = promise.get_future().get();
    if (error)
    {
        throwReadError("list registered tables", *error);
    }
    return tables;
}

std::optional<Entry> getRow(StorageInterface& storage, std::string_view table, std::string_view key)
{
    std::promise<std::pair<Error::UniquePtr, std::optional<Entry>>> promise;
    storage.asyncGetRow(table, key, [&promise](Error::UniquePtr error, std::optional<Entry> entry) {
        promise.set_value({std::move(error), std::move(entry)});
    });
    auto [error, entry] = promise.get_future().get();
    if (error)
    {
        throwReadError("read " + std::string(table) + ":" + std::string(key), *error);
    }
    return entry;
}

[[noreturn]] void throwCorruptMetadata(std::string const& table, std::string const& pageKey)
{
    BOOST_THROW_EXCEPTION(BCOS_ERROR(StorageError::DatabaseError,
        "Inconsistent KeyPage metadata in table " + table + ", page 0x" + toHex(pageKey) +
            "; verify storage integrity and set storage.key_page_size explicitly for the existing "
            "data format"));
}
}  // namespace

bool hasKeyPageMetadata(StorageInterface& storage, std::size_t* examinedTables)
{
    std::size_t examined = 0;
    auto finish = [&]() {
        if (examinedTables)
        {
            *examinedTables = examined;
        }
    };
    for (auto const& table : getRegisteredTables(storage))
    {
        if (table == StorageInterface::SYS_TABLES)
        {
            continue;
        }
        ++examined;
        auto metaEntry = getRow(storage, table, TABLE_META_KEY);
        if (!metaEntry)
        {
            continue;
        }
        auto meta = decodeArchive(metaEntry->get());
        if (!meta)
        {
            continue;
        }
        auto& pageInfos = meta->getAllPageInfoNoLock();
        if (pageInfos.empty())
        {
            continue;
        }
        auto const& pageInfo = pageInfos.front();
        auto pageKey = pageInfo.getPageKey();
        if (pageKey.empty() || pageInfo.getCount() == 0)
        {
            continue;
        }
        auto pageEntry = getRow(storage, table, pageKey);
        if (!pageEntry)
        {
            throwCorruptMetadata(table, pageKey);
        }
        auto page = decodeArchive(pageEntry->get(), pageKey);
        if (!page)
        {
            continue;
        }
        if (page->endKey() != pageKey || page->validCount() != pageInfo.getCount() ||
            page->count() != page->validCount())
        {
            throwCorruptMetadata(table, pageKey);
        }
        if (!hasExactEncoding(*page, pageEntry->get()))
        {
            continue;
        }
        finish();
        return true;
    }
    finish();
    return false;
}
}  // namespace bcos::storage
