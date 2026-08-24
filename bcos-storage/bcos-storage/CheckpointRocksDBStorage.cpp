#include "bcos-storage/CheckpointRocksDBStorage.h"
#include "bcos-storage/StateKVResolver.h"
#include <rocksdb/filter_policy.h>
#include <rocksdb/statistics.h>
#include <rocksdb/table.h>
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
}  // namespace detail


template <class KeyType, class ValueType, Resolver<KeyType> KeyResolver,
    Resolver<ValueType> ValueResolver>
::rocksdb::Options CheckpointRocksDBStorage<KeyType, ValueType, KeyResolver, ValueResolver>::latestCheckpointOptions() const
{
    ::rocksdb::Options options;
    options.create_if_missing = true;

    if (m_option.optimizeLevelStyleCompaction)
    {
        options.IncreaseParallelism();
        options.OptimizeLevelStyleCompaction();
    }

    options.max_background_jobs = m_option.maxBackgroundJobs;
    options.max_write_buffer_number = m_option.maxWriteBufferNumber;
    options.enable_blob_files = m_option.enableBlobFiles;
    options.bytes_per_sync = 1 << 20;
    options.compression = ::rocksdb::kZSTD;
    options.bottommost_compression = ::rocksdb::kZSTD;
    options.max_open_files = 256;
    options.write_buffer_size = m_option.writeBufferSize;
    options.min_write_buffer_number_to_merge = m_option.minWriteBufferNumberToMerge;
    options.enable_pipelined_write = true;
    options.max_bytes_for_level_base = 512 << 20;
    options.target_file_size_base = 128 << 20;

    if (m_option.enableDBStatistics)
    {
        options.statistics = ::rocksdb::CreateDBStatistics();
    }

    ::rocksdb::BlockBasedTableOptions table_options;
    table_options.block_cache = m_cache;
    table_options.filter_policy.reset(::rocksdb::NewBloomFilterPolicy(10, false));
    table_options.optimize_filters_for_memory = true;
    table_options.block_size = 64 * 1024;
    options.table_factory.reset(::rocksdb::NewBlockBasedTableFactory(table_options));

    return options;
}


template <class KeyType, class ValueType, Resolver<KeyType> KeyResolver,
    Resolver<ValueType> ValueResolver>
::rocksdb::Options CheckpointRocksDBStorage<KeyType, ValueType, KeyResolver, ValueResolver>::historicalCheckpointOptions() const
{
    ::rocksdb::Options options;
    options.create_if_missing = false;

    options.max_open_files = 256;
    options.compression = ::rocksdb::kZSTD;
    options.bottommost_compression = ::rocksdb::kZSTD;

    ::rocksdb::BlockBasedTableOptions table_options;
    table_options.block_cache = m_cache;
    table_options.filter_policy.reset(::rocksdb::NewBloomFilterPolicy(10, false));
    table_options.optimize_filters_for_memory = true;
    table_options.block_size = 64 * 1024;
    options.table_factory.reset(::rocksdb::NewBlockBasedTableFactory(table_options));

    if (m_option.enableDBStatistics)
    {
        options.statistics = ::rocksdb::CreateDBStatistics();
    }

    return options;
}

namespace detail
{
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

template class CheckpointRocksDBStorage<bcos::executor_v1::StateKey, bcos::storage::Entry,
    bcos::storage2::rocksdb::StateKeyResolver, bcos::storage2::rocksdb::StateValueResolver>;

}  // namespace bcos::storage2::rocksdb
