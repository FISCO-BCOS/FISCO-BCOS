#pragma once

#include "bcos-framework/storage2/AnyStorage.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/MultiLayerStorage.h"
#include "bcos-storage/CheckpointRocksDBStorage.h"
#include "bcos-storage/StateKVResolver.h"
#include <bcos-framework/transaction-executor/StateKey.h>
#include <memory>
#include <optional>
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

    explicit GlobalStateStorageInitializer(
        std::string const& storageRootPath,
        bcos::storage2::rocksdb::RocksDBCheckpointOption const& rocksDBOption = {});

    static Ptr build(std::string const& storageRootPath,
        bcos::storage2::rocksdb::RocksDBCheckpointOption const& rocksDBOption = {});

    GlobalStateStorage& storage() { return m_storage; }
    GlobalStateStorage const& storage() const { return m_storage; }

    ::rocksdb::DB& rocksDB() { return m_storage.latestBackend().rocksDB(); }

private:
    GlobalStateCacheStorage m_cacheStorage;
    GlobalStateCheckpointStorage m_checkpointStorage;
    GlobalStateStorage m_storage;
};

/// Fork a fresh LATEST view of a GlobalStateStorage and type-erase it into an AnyStorage
/// handle that OWNS the view: the AnyStorage's erased reference points into the view, so the
/// view must outlive the handle — the aliasing shared_ptr ties both to the same owner.
/// eth_getStorageAt's latest-state path (NodeService::StateStorageProvider) uses this to get
/// a consistent point-in-time snapshot per request (pending layers -> cache -> backend).
template <class ViewType>
std::shared_ptr<bcos::storage2::AnyStorage<executor_v1::StateKey, executor_v1::StateValue>>
forkLatestStateView(ViewType view)
{
    using AnyStateStorage =
        bcos::storage2::AnyStorage<executor_v1::StateKey, executor_v1::StateValue>;
    struct OwningView
    {
        ViewType view;
        std::optional<AnyStateStorage> erased;

        explicit OwningView(ViewType v) : view(std::move(v)) { erased.emplace(view); }
    };
    auto owner = std::make_shared<OwningView>(std::move(view));
    return {owner, std::addressof(*owner->erased)};
}
}  // namespace bcos::initializer
