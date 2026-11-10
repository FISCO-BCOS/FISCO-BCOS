/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "KeyPageStorageDetector.h"
#include "KeyPageStorage.h"
#include "StateStorageFactory.h"
#include <bcos-framework/storage/Serialize.h>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/throw_exception.hpp>
#include <cstdint>
#include <future>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bcos::storage
{
namespace
{
template <typename T>
std::optional<T> decodeArchive(std::string_view value)
{
    if (value.empty())
    {
        return std::nullopt;
    }
    try
    {
        boost::iostreams::stream<boost::iostreams::array_source> inputStream(
            value.data(), value.size());
        boost::archive::binary_iarchive archive(inputStream, serialize::ARCHIVE_FLAG);
        T output;
        archive >> output;
        if (inputStream.peek() != std::char_traits<char>::eof())
        {
            return std::nullopt;
        }
        return std::optional<T>(std::move(output));
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
    BOOST_THROW_EXCEPTION(BCOS_ERROR(StorageError::UnknownEntryType,
        "Inconsistent KeyPage metadata in table " + table + ", page " + pageKey));
}

std::set<std::string, std::less<>> const& ignoredTables()
{
    static auto const tables = [] {
        std::set<std::string, std::less<>> ignored;
        ignored.insert(IGNORED_ARRAY.begin(), IGNORED_ARRAY.end());
        ignored.insert(IGNORED_ARRAY_310.begin(), IGNORED_ARRAY_310.end());
        ignored.insert(tool::FS_ROOT_SUBS.begin(), tool::FS_ROOT_SUBS.end());
        return ignored;
    }();
    return tables;
}
}  // namespace

bool hasKeyPageMetadata(StorageInterface& storage)
{
    auto const& ignored = ignoredTables();
    for (auto const& table : getRegisteredTables(storage))
    {
        if (ignored.contains(table))
        {
            continue;
        }
        auto metaEntry = getRow(storage, table, TABLE_META_KEY);
        if (!metaEntry)
        {
            continue;
        }
        auto meta = decodeArchive<KeyPageStorage::TableMeta>(metaEntry->get());
        if (!meta)
        {
            continue;
        }
        auto& pageInfos = meta->getAllPageInfoNoLock();
        if (pageInfos.empty())
        {
            continue;
        }
        for (auto const& pageInfo : pageInfos)
        {
            auto pageKey = pageInfo.getPageKey();
            if (pageKey.empty() || pageInfo.getCount() == 0)
            {
                throwCorruptMetadata(table, pageKey);
            }
            auto pageEntry = getRow(storage, table, pageKey);
            if (!pageEntry)
            {
                throwCorruptMetadata(table, pageKey);
            }
            auto page = decodeArchive<KeyPageStorage::Page>(pageEntry->get());
            if (!page || page->endKey() != pageKey || page->validCount() != pageInfo.getCount() ||
                page->count() != page->validCount())
            {
                throwCorruptMetadata(table, pageKey);
            }
            return true;
        }
    }
    return false;
}
}  // namespace bcos::storage
