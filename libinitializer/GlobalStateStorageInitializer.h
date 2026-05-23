#pragma once

#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/MultiLayerStorage.h"
#include "bcos-storage/CheckpointRocksDBStorage.h"
#include "bcos-storage/StateKVResolver.h"
#include <bcos-framework/transaction-executor/StateKey.h>
#include <memory>
#include <string>

namespace bcos::initializer
{
using GlobalStateMutableStorage =
    bcos::storage2::memory_storage::MemoryStorage<bcos::executor_v1::StateKey,
        bcos::executor_v1::StateValue,
        bcos::storage2::memory_storage::ORDERED |
            bcos::storage2::memory_storage::LOGICAL_DELETION>;

using GlobalStateCacheStorage =
    bcos::storage2::memory_storage::MemoryStorage<bcos::executor_v1::StateKey,
        bcos::executor_v1::StateValue,
        bcos::storage2::memory_storage::CONCURRENT | bcos::storage2::memory_storage::LRU>;

using GlobalStateCheckpointStorage =
    bcos::storage2::rocksdb::CheckpointRocksDBStorage<bcos::executor_v1::StateKey,
        bcos::executor_v1::StateValue, bcos::storage2::rocksdb::StateKeyResolver,
        bcos::storage2::rocksdb::StateValueResolver>;

using GlobalStateStorage =
    bcos::storage2::MultiLayerStorage<GlobalStateMutableStorage, GlobalStateCacheStorage,
        GlobalStateCheckpointStorage>;

class GlobalStateStorageInitializer
{
public:
    using Ptr = std::shared_ptr<GlobalStateStorageInitializer>;

    explicit GlobalStateStorageInitializer(std::string const& storageRootPath);

    static Ptr build(std::string const& storageRootPath);

    GlobalStateStorage& storage() { return m_storage; }
    GlobalStateStorage const& storage() const { return m_storage; }

    ::rocksdb::DB& rocksDB() { return m_storage.latestBackend().rocksDB(); }

private:
    GlobalStateCacheStorage m_cacheStorage;
    GlobalStateCheckpointStorage m_checkpointStorage;
    GlobalStateStorage m_storage;
};
}  // namespace bcos::initializer