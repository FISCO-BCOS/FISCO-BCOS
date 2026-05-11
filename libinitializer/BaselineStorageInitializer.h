#pragma once

#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/MultiLayerStorage.h"
#include "bcos-storage/RocksDBStorage2.h"
#include "bcos-storage/StateKVResolver.h"
#include <bcos-framework/transaction-executor/StateKey.h>
#include <rocksdb/db.h>
#include <memory>

namespace bcos::initializer
{
using BaselineSchedulerMutableStorage =
    bcos::storage2::memory_storage::MemoryStorage<bcos::executor_v1::StateKey,
        bcos::executor_v1::StateValue,
        bcos::storage2::memory_storage::ORDERED |
            bcos::storage2::memory_storage::LOGICAL_DELETION>;

using BaselineSchedulerCacheStorage =
    bcos::storage2::memory_storage::MemoryStorage<bcos::executor_v1::StateKey,
        bcos::executor_v1::StateValue,
        bcos::storage2::memory_storage::CONCURRENT | bcos::storage2::memory_storage::LRU>;

using BaselineSchedulerBackendStorage = bcos::storage2::rocksdb::RocksDBStorage2<
    bcos::executor_v1::StateKey, bcos::executor_v1::StateValue,
    bcos::storage2::rocksdb::StateKeyResolver, bcos::storage2::rocksdb::StateValueResolver>;

using BaselineSchedulerMultiLayerStorage =
    bcos::storage2::MultiLayerStorage<BaselineSchedulerMutableStorage,
        BaselineSchedulerCacheStorage, BaselineSchedulerBackendStorage>;

class BaselineStorageInitializer
{
public:
    using Ptr = std::shared_ptr<BaselineStorageInitializer>;

    explicit BaselineStorageInitializer(::rocksdb::DB& rocksDB);

    static Ptr build(::rocksdb::DB& rocksDB);

    BaselineSchedulerMultiLayerStorage& storage() { return m_multiLayerStorage; }
    BaselineSchedulerMultiLayerStorage const& storage() const { return m_multiLayerStorage; }

private:
    BaselineSchedulerCacheStorage m_cacheStorage;
    BaselineSchedulerBackendStorage m_rocksDBStorage;
    BaselineSchedulerMultiLayerStorage m_multiLayerStorage;
};
}  // namespace bcos::initializer