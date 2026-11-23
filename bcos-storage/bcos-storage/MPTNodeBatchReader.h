#pragma once

#include "KeyPrefixes.h"
#include "RocksDBStorage2.h"
#include <rocksdb/db.h>
#include <rocksdb/slice.h>
#include <rocksdb/status.h>
#include <boost/throw_exception.hpp>
#include <array>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

namespace bcos::storage2
{
inline std::vector<std::optional<bcos::bytes>> readMPTNodeBatch(
    ::rocksdb::DB& rocksDB, std::span<bcos::h256 const> hashes)
{
    if (hashes.empty())
    {
        return {};
    }
    std::vector<std::array<char, kMPTKeyLength>> encodedKeys(hashes.size());
    std::vector<::rocksdb::Slice> rocksDBKeys;
    rocksDBKeys.reserve(hashes.size());
    for (size_t i = 0; i < hashes.size(); ++i)
    {
        auto& key = encodedKeys[i];
        std::memcpy(key.data(), kMPTTable.data(), kMPTTable.size());
        key[kMPTTable.size()] = ':';
        std::memcpy(key.data() + kMPTTable.size() + 1, hashes[i].data(), bcos::h256::SIZE);
        rocksDBKeys.emplace_back(key.data(), key.size());
    }

    std::vector<::rocksdb::PinnableSlice> results(hashes.size());
    std::vector<::rocksdb::Status> statuses(hashes.size());
    ::rocksdb::ReadOptions readOptions;
    readOptions.fill_cache = false;
    rocksDB.MultiGet(readOptions, rocksDB.DefaultColumnFamily(), rocksDBKeys.size(),
        rocksDBKeys.data(), results.data(), statuses.data());

    std::vector<std::optional<bcos::bytes>> values(hashes.size());
    for (size_t i = 0; i < hashes.size(); ++i)
    {
        auto const& status = statuses[i];
        if (!status.ok())
        {
            if (status.IsNotFound())
            {
                continue;
            }
            BOOST_THROW_EXCEPTION(
                rocksdb::RocksDBException{} << bcos::Error::ErrorMessage(status.ToString()));
        }
        auto const raw = results[i].ToStringView();
        values[i].emplace(reinterpret_cast<bcos::byte const*>(raw.data()),
            reinterpret_cast<bcos::byte const*>(raw.data()) + raw.size());
    }
    return values;
}
}  // namespace bcos::storage2
