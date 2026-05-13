#pragma once

#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/MultiLayerStorage.h"
#include "bcos-storage/RocksDBStorage2.h"
#include "bcos-storage/StateKVResolver.h"
#include <bcos-framework/transaction-executor/StateKey.h>
#include <memory>
#include <rocksdb/db.h>

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

using GlobalStateBackendStorage = bcos::storage2::rocksdb::RocksDBStorage2<
    bcos::executor_v1::StateKey, bcos::executor_v1::StateValue,
    bcos::storage2::rocksdb::StateKeyResolver, bcos::storage2::rocksdb::StateValueResolver>;

using GlobalStateStorage =
    bcos::storage2::MultiLayerStorage<GlobalStateMutableStorage, GlobalStateCacheStorage,
        GlobalStateBackendStorage>;

class GlobalStateStorageInitializer
{
public:
    using Ptr = std::shared_ptr<GlobalStateStorageInitializer>;

    explicit GlobalStateStorageInitializer(::rocksdb::DB& rocksDB);

    static Ptr build(::rocksdb::DB& rocksDB);

    GlobalStateStorage& storage() { return m_storage; }
    GlobalStateStorage const& storage() const { return m_storage; }

private:
    GlobalStateCacheStorage m_cacheStorage;
    GlobalStateBackendStorage m_rocksDBStorage;
    GlobalStateStorage m_storage;
};
}  // namespace bcos::initializer