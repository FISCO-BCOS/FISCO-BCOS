#pragma once

#include "bcos-storage/RocksDBStorage2.h"
#include "bcos-utilities/Exceptions.h"
#include "bcos-utilities/FixedBytes.h"
#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace bcos::storage2::rocksdb
{

DERIVE_BCOS_EXCEPTION(CheckpointRocksDBException);


struct RocksDBCheckpointOption
{
    int maxWriteBufferNumber = 3;
    int maxBackgroundJobs = 4;
    size_t writeBufferSize = 64 << 20;  // 64MB
    int minWriteBufferNumberToMerge = 1;
    size_t blockCacheSize = 128 << 20;  // 128MB
    bool optimizeLevelStyleCompaction = false;
    bool enableBlobFiles = false;
    bool enableDBStatistics = false;
    // RocksDB table-cache bound. The default 256 caps fd usage and pinned index/filter
    // blocks for ordinary consortium-chain databases. -1 (unlimited) never evicts table
    // readers — the right choice for an archive-scale DB (tens of thousands of SSTs),
    // where a 256-entry cache thrashes: every random read evicts a reader and re-reads
    // its index/filter/properties blocks (observed ~900MB/s of throwaway reads during an
    // MPT prune rebuild on a ~942GB / 10k-SST database). Cost of -1: fd usage grows
    // toward the live SST count and, with cache_index_and_filter_blocks off, every
    // reader pins its index/filter blocks on the heap — raise the process nofile limit
    // (>= 65536) when configuring it via [storage].rocksdb_max_open_files.
    int maxOpenFiles = 256;
};

namespace detail
{
// The process soft RLIMIT_NOFILE, or -1 when unlimited / undeterminable.
long softOpenFileLimit();

// With maxOpenFiles == -1 RocksDB never evicts table readers, so fd usage grows toward the
// live SST count. Log a loud warning when the soft fd limit leaves no headroom for an
// archive-scale SST count; a no-op for any bounded value.
void warnIfMaxOpenFilesUnbounded(int maxOpenFiles, std::string_view path);

std::unique_ptr<::rocksdb::DB> openCheckpointRocksDB(
    const std::string& path, const ::rocksdb::Options& options, bool readOnly);

::rocksdb::Options latestCheckpointOptions(const RocksDBCheckpointOption& option);

::rocksdb::Options historicalCheckpointOptions(const RocksDBCheckpointOption& option);

void ensureCheckpointDirectories(std::string_view rootDir);

std::string resolveLatestCheckpointPath(std::string_view rootDir);

std::string resolveHistoricalCheckpointPath(
    std::string_view rootDir, bcos::h256 const& checkpointName);

void createHistoricalCheckpoint(
    ::rocksdb::DB& latestStorage, std::string_view rootDir, bcos::h256 const& checkpointName);

void deleteHistoricalCheckpoint(std::string_view rootDir, bcos::h256 const& checkpointName);

std::optional<bcos::h256> findCheckpointByTime(std::string_view rootDir, bool latest);
}  // namespace detail

template <class KeyType, class ValueType, Resolver<KeyType> KeyResolver,
    Resolver<ValueType> ValueResolver>
class CheckpointRocksDBStorage
{
public:
    using Storage = RocksDBStorage2<KeyType, ValueType, KeyResolver, ValueResolver>;
    using CheckpointName = bcos::h256;


    explicit CheckpointRocksDBStorage(
        std::string_view rootDir, KeyResolver keyResolver = {}, ValueResolver valueResolver = {},
        RocksDBCheckpointOption option = {})
      : m_path(std::filesystem::path(rootDir).lexically_normal().string()),
        m_option(std::move(option)),
        m_cache(::rocksdb::NewLRUCache(m_option.blockCacheSize)),
        m_keyResolver(std::move(keyResolver)),
        m_valueResolver(std::move(valueResolver))
    {
        detail::ensureCheckpointDirectories(m_path);
    }

    const std::string& path() const noexcept { return m_path; }

    std::string latestPath() const { return detail::resolveLatestCheckpointPath(m_path); }

    std::string checkpointPath(CheckpointName const& checkpointName) const
    {
        return detail::resolveHistoricalCheckpointPath(m_path, checkpointName);
    }


    Storage open()
    {
        return Storage(detail::openCheckpointRocksDB(detail::resolveLatestCheckpointPath(m_path),
                           latestCheckpointOptions(), false),
            m_keyResolver, m_valueResolver);
    }

    Storage open(CheckpointName const& checkpointName)
    {
        return Storage(detail::openCheckpointRocksDB(
                           detail::resolveHistoricalCheckpointPath(m_path, checkpointName),
                           historicalCheckpointOptions(), true),
            m_keyResolver, m_valueResolver);
    }

    ::rocksdb::Options latestCheckpointOptions() const;
    ::rocksdb::Options historicalCheckpointOptions() const;

    void createCheckpoint(Storage& latestStorage, CheckpointName const& checkpointName)
    {
        detail::createHistoricalCheckpoint(latestStorage.rocksDB(), m_path, checkpointName);
    }

    void deleteCheckpoint(CheckpointName const& checkpointName)
    {
        detail::deleteHistoricalCheckpoint(m_path, checkpointName);
    }

    std::optional<CheckpointName> latestCheckpointName() const
    {
        return detail::findCheckpointByTime(m_path, true);
    }

    std::optional<CheckpointName> oldestCheckpointName() const
    {
        return detail::findCheckpointByTime(m_path, false);
    }

private:
    std::string m_path;
    RocksDBCheckpointOption m_option;
    std::shared_ptr<::rocksdb::Cache> m_cache;
    [[no_unique_address]] KeyResolver m_keyResolver;
    [[no_unique_address]] ValueResolver m_valueResolver;
};

}  // namespace bcos::storage2::rocksdb