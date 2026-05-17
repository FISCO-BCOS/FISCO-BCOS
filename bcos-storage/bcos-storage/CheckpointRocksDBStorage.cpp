#include "bcos-storage/CheckpointRocksDBStorage.h"
#include "bcos-utilities/Exceptions.h"
#include <rocksdb/options.h>
#include <boost/throw_exception.hpp>
#include <filesystem>

namespace bcos::storage2::rocksdb
{

CheckpointRocksDBStorage::CheckpointRocksDBStorage(std::string_view rootDir)
  : m_path(std::filesystem::path(rootDir).lexically_normal().string())
{}

const std::string& CheckpointRocksDBStorage::path() const noexcept
{
    return m_path;
}

std::string CheckpointRocksDBStorage::latestPath() const
{
    return resolveLatestPath(m_path);
}

std::string CheckpointRocksDBStorage::checkpointPath(std::string_view checkpointName) const
{
    return resolveCheckpointPath(m_path, checkpointName);
}

std::unique_ptr<::rocksdb::DB> CheckpointRocksDBStorage::open(
    LatestCheckpointStorageTag latestTag) const
{
    return openRocksDB(resolveLatestPath(m_path), latestOptions(latestTag));
}

std::unique_ptr<::rocksdb::DB> CheckpointRocksDBStorage::open(std::string_view checkpointName) const
{
    return openRocksDB(resolveCheckpointPath(m_path, checkpointName), checkpointOptions());
}

std::unique_ptr<::rocksdb::DB> CheckpointRocksDBStorage::openRocksDB(
    const std::string& path, const ::rocksdb::Options& options)
{
    ::rocksdb::DB* rocksDB = nullptr;
    auto status = ::rocksdb::DB::Open(options, path, &rocksDB);
    if (!status.ok())
    {
        BOOST_THROW_EXCEPTION(
            CheckpointRocksDBException{} << bcos::errinfo_comment(
                "Open rocksdb failed, path: " + path + ", error: " + status.ToString()));
    }
    return std::unique_ptr<::rocksdb::DB>(rocksDB);
}

::rocksdb::Options CheckpointRocksDBStorage::latestOptions(LatestCheckpointStorageTag /*latestTag*/)
{
    ::rocksdb::Options options;
    options.create_if_missing = true;
    return options;
}

::rocksdb::Options CheckpointRocksDBStorage::checkpointOptions()
{
    ::rocksdb::Options options;
    options.create_if_missing = false;
    return options;
}

std::string CheckpointRocksDBStorage::resolveLatestPath(std::string_view rootDir)
{
    auto path = std::filesystem::path(rootDir) / "latest";
    return path.lexically_normal().string();
}

std::string CheckpointRocksDBStorage::resolveCheckpointPath(
    std::string_view rootDir, std::string_view checkpointName)
{
    auto path =
        std::filesystem::path(rootDir) / "checkpoints" / std::filesystem::path(checkpointName);
    return path.lexically_normal().string();
}

}  // namespace bcos::storage2::rocksdb