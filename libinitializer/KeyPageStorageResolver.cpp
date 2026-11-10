/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "KeyPageStorageResolver.h"
#include <bcos-table/src/KeyPageStorageDetector.h>
#include <bcos-tool/Exceptions.h>
#include <rocksdb/db.h>
#include <boost/throw_exception.hpp>
#include <charconv>
#include <optional>
#include <string>

namespace bcos::initializer
{
namespace
{
std::optional<std::string> readMarker(::rocksdb::DB& stateDB)
{
    std::string value;
    auto status = stateDB.Get(::rocksdb::ReadOptions{},
        ::rocksdb::Slice(KEY_PAGE_SIZE_MARKER_KEY.data(), KEY_PAGE_SIZE_MARKER_KEY.size()), &value);
    if (status.IsNotFound())
    {
        return std::nullopt;
    }
    if (!status.ok())
    {
        BOOST_THROW_EXCEPTION(
            tool::InvalidConfig() << errinfo_comment(
                "failed to read the stored key_page_size marker: " + status.ToString()));
    }
    return value;
}

void writeMarker(::rocksdb::DB& stateDB, size_t value)
{
    auto text = std::to_string(value);
    ::rocksdb::WriteOptions options;
    options.sync = true;
    auto status = stateDB.Put(options,
        ::rocksdb::Slice(KEY_PAGE_SIZE_MARKER_KEY.data(), KEY_PAGE_SIZE_MARKER_KEY.size()),
        ::rocksdb::Slice(text.data(), text.size()));
    if (!status.ok())
    {
        BOOST_THROW_EXCEPTION(
            tool::InvalidConfig() << errinfo_comment(
                "failed to persist the resolved key_page_size: " + status.ToString()));
    }
}

size_t parseMarker(std::string const& value)
{
    size_t parsed = 0;
    auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error != std::errc{} || end != value.data() + value.size() ||
        (parsed != 0 && (parsed < tool::NodeConfig::MIN_KEY_PAGE_SIZE ||
                            parsed > tool::NodeConfig::MAX_KEY_PAGE_SIZE)))
    {
        BOOST_THROW_EXCEPTION(tool::InvalidConfig() << errinfo_comment(
                                  "invalid stored key_page_size marker '" + value +
                                  "'; set storage.key_page_size explicitly in config.ini"));
    }
    return parsed;
}
}  // namespace

KeyPageSizeResolution resolveKeyPageSize(
    tool::NodeConfig& config, storage::StorageInterface& storage, ::rocksdb::DB& stateDB)
{
    if (config.keyPageSizeConfigured())
    {
        auto value = config.keyPageSize();
        writeMarker(stateDB, value);
        return {value, "config", 0};
    }
    if (auto marker = readMarker(stateDB))
    {
        auto value = parseMarker(*marker);
        config.setKeyPageSize(value);
        return {value, "marker", 0};
    }

    size_t examined = 0;
    auto hasMetadata = storage::hasKeyPageMetadata(storage, &examined);
    size_t const value = hasMetadata ? 10240 : 0;
    config.setKeyPageSize(value);
    writeMarker(stateDB, value);
    return {value, "detection", examined};
}
}  // namespace bcos::initializer
