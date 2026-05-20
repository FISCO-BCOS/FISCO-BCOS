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

namespace detail
{
std::unique_ptr<::rocksdb::DB> openCheckpointRocksDB(
    const std::string& path, const ::rocksdb::Options& options, bool readOnly);

::rocksdb::Options latestCheckpointOptions();

::rocksdb::Options historicalCheckpointOptions();

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
        std::string_view rootDir, KeyResolver keyResolver = {}, ValueResolver valueResolver = {})
      : m_path(std::filesystem::path(rootDir).lexically_normal().string()),
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
                           detail::latestCheckpointOptions(), false),
            m_keyResolver, m_valueResolver);
    }

    Storage open(CheckpointName const& checkpointName)
    {
        return Storage(detail::openCheckpointRocksDB(
                           detail::resolveHistoricalCheckpointPath(m_path, checkpointName),
                           detail::historicalCheckpointOptions(), true),
            m_keyResolver, m_valueResolver);
    }

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
    [[no_unique_address]] KeyResolver m_keyResolver;
    [[no_unique_address]] ValueResolver m_valueResolver;
};

}  // namespace bcos::storage2::rocksdb