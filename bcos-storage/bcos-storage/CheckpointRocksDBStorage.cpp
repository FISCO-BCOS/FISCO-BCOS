#include "bcos-storage/CheckpointRocksDBStorage.h"
#include <rocksdb/utilities/checkpoint.h>
#include <boost/throw_exception.hpp>
#include <filesystem>
#include <optional>

namespace bcos::storage2::rocksdb
{
namespace
{
std::string resolveCheckpointsPath(std::string_view rootDir)
{
    auto path = std::filesystem::path(rootDir) / "checkpoints";
    return path.lexically_normal().string();
}

bool isExistingRocksDB(std::string_view path)
{
    // RocksDB always writes a CURRENT file at the database root
    return std::filesystem::exists(std::filesystem::path(path) / "CURRENT");
}

std::optional<bcos::h256> parseCheckpointName(std::string const& checkpointName)
{
    try
    {
        return bcos::h256(checkpointName);
    }
    catch (...)
    {
        return std::nullopt;
    }
}
}  // namespace

namespace detail
{
std::unique_ptr<::rocksdb::DB> openCheckpointRocksDB(
    const std::string& path, const ::rocksdb::Options& options, bool readOnly)
{
    ::rocksdb::DB* rocksDB = nullptr;
    auto status = readOnly ? ::rocksdb::DB::OpenForReadOnly(options, path, &rocksDB) :
                             ::rocksdb::DB::Open(options, path, &rocksDB);
    if (!status.ok())
    {
        BOOST_THROW_EXCEPTION(
            CheckpointRocksDBException{} << bcos::errinfo_comment(
                "Open rocksdb failed, path: " + path + ", error: " + status.ToString()));
    }
    return std::unique_ptr<::rocksdb::DB>(rocksDB);
}

::rocksdb::Options latestCheckpointOptions()
{
    ::rocksdb::Options options;
    options.create_if_missing = true;
    return options;
}

::rocksdb::Options historicalCheckpointOptions()
{
    ::rocksdb::Options options;
    options.create_if_missing = false;
    return options;
}

void ensureCheckpointDirectories(std::string_view rootDir)
{
    auto normalizedRoot = std::filesystem::path(rootDir).lexically_normal();
    std::filesystem::create_directories(normalizedRoot);
    std::filesystem::create_directories(normalizedRoot / "checkpoints");

    // For new-style paths, also ensure the "latest" subdirectory exists.
    // For old-style paths (rootDir itself contains RocksDB data), skip this
    // so the existing data is used directly.
    if (!isExistingRocksDB(normalizedRoot.string()))
    {
        std::filesystem::create_directories(normalizedRoot / "latest");
    }
}

std::string resolveLatestCheckpointPath(std::string_view rootDir)
{
    // Backward compatibility: if rootDir itself contains a RocksDB database
    // (old-style layout), use it directly as the "latest" store.
    // Otherwise use the new-style rootDir/latest/ layout.
    if (isExistingRocksDB(rootDir))
    {
        return std::filesystem::path(rootDir).lexically_normal().string();
    }
    auto path = std::filesystem::path(rootDir) / "latest";
    return path.lexically_normal().string();
}

std::string resolveHistoricalCheckpointPath(
    std::string_view rootDir, bcos::h256 const& checkpointName)
{
    auto path = std::filesystem::path(rootDir) / "checkpoints" /
                std::filesystem::path(checkpointName.hex());
    return path.lexically_normal().string();
}

void createHistoricalCheckpoint(
    ::rocksdb::DB& latestStorage, std::string_view rootDir, bcos::h256 const& checkpointName)
{
    auto path = resolveHistoricalCheckpointPath(rootDir, checkpointName);
    if (std::filesystem::exists(path))
    {
        BOOST_THROW_EXCEPTION(CheckpointRocksDBException{} << bcos::errinfo_comment(
                                  "Checkpoint already exists: " + checkpointName.hex()));
    }

    ::rocksdb::Checkpoint* rawCheckpoint = nullptr;
    auto status = ::rocksdb::Checkpoint::Create(&latestStorage, &rawCheckpoint);
    if (!status.ok())
    {
        BOOST_THROW_EXCEPTION(
            CheckpointRocksDBException{} << bcos::errinfo_comment(
                "Create rocksdb checkpoint handler failed, error: " + status.ToString()));
    }

    std::unique_ptr<::rocksdb::Checkpoint> checkpointGuard(rawCheckpoint);
    status = checkpointGuard->CreateCheckpoint(path);
    if (!status.ok())
    {
        BOOST_THROW_EXCEPTION(CheckpointRocksDBException{} << bcos::errinfo_comment(
                                  "Create rocksdb checkpoint failed, path: " + path +
                                  ", error: " + status.ToString()));
    }
}

void deleteHistoricalCheckpoint(std::string_view rootDir, bcos::h256 const& checkpointName)
{
    std::error_code errorCode;
    std::filesystem::remove_all(
        resolveHistoricalCheckpointPath(rootDir, checkpointName), errorCode);
    if (errorCode)
    {
        BOOST_THROW_EXCEPTION(CheckpointRocksDBException{} << bcos::errinfo_comment(
                                  "Delete checkpoint failed, name: " + checkpointName.hex() +
                                  ", error: " + errorCode.message()));
    }
}

std::optional<bcos::h256> findCheckpointByTime(std::string_view rootDir, bool latest)
{
    std::error_code errorCode;
    auto checkpointsPath = resolveCheckpointsPath(rootDir);
    if (!std::filesystem::exists(checkpointsPath, errorCode) || errorCode)
    {
        return std::nullopt;
    }

    std::optional<std::pair<bcos::h256, std::filesystem::file_time_type>> selected;
    for (auto const& entry : std::filesystem::directory_iterator(checkpointsPath, errorCode))
    {
        if (errorCode)
        {
            break;
        }

        if (!entry.is_directory(errorCode) || errorCode)
        {
            errorCode.clear();
            continue;
        }

        auto checkpoint = parseCheckpointName(entry.path().filename().string());
        if (!checkpoint)
        {
            continue;
        }

        auto time = entry.last_write_time(errorCode);
        if (errorCode)
        {
            errorCode.clear();
            continue;
        }

        if (!selected || (latest ? time > selected->second : time < selected->second))
        {
            selected = std::make_pair(*checkpoint, time);
        }
    }

    return selected ? std::make_optional(selected->first) : std::nullopt;
}
}  // namespace detail
}  // namespace bcos::storage2::rocksdb
