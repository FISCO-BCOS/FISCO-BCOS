#pragma once

#include "bcos-utilities/Exceptions.h"
#include <rocksdb/db.h>
#include <memory>
#include <string>
#include <string_view>

namespace bcos::storage2::rocksdb
{

DERIVE_BCOS_EXCEPTION(CheckpointRocksDBException);

inline constexpr struct LatestCheckpointStorageTag
{
} latestCheckpointStorage;

class CheckpointRocksDBStorage
{
public:
    explicit CheckpointRocksDBStorage(std::string_view rootDir);

    const std::string& path() const noexcept;

    std::string latestPath() const;

    std::string checkpointPath(std::string_view checkpointName) const;

    std::unique_ptr<::rocksdb::DB> open(LatestCheckpointStorageTag latestTag) const;

    std::unique_ptr<::rocksdb::DB> open(std::string_view checkpointName) const;

private:
    static std::unique_ptr<::rocksdb::DB> openRocksDB(
        const std::string& path, const ::rocksdb::Options& options);

    static ::rocksdb::Options latestOptions(LatestCheckpointStorageTag latestTag);

    static ::rocksdb::Options checkpointOptions();

    static std::string resolveLatestPath(std::string_view rootDir);

    static std::string resolveCheckpointPath(
        std::string_view rootDir, std::string_view checkpointName);

    std::string m_path;
};

}  // namespace bcos::storage2::rocksdb