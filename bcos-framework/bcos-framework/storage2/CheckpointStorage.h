#pragma once

#include "bcos-framework/storage2/Storage.h"
#include "bcos-utilities/FixedBytes.h"
#include <concepts>
#include <optional>

namespace bcos::storage2
{

/// @brief Concept for checkpoint-aware storage managers.
///
/// A CheckpointStorage is a factory/manager that can:
/// - Open the latest (writable) storage snapshot via open()
/// - Open a historical (read-only) storage snapshot via open(name)
/// - Create named checkpoints from a live storage instance
/// - Delete named checkpoints
/// - Query the latest and oldest checkpoint names
///
/// This concept is designed to work with implementations like
/// CheckpointRocksDBStorage, which wraps RocksDB checkpoints.
///
/// @tparam CheckpointStore  The checkpoint manager type
/// @tparam Key              Key type used by the underlying storage
/// @tparam Value            Value type used by the underlying storage
/// @tparam CheckpointName   Type used to name checkpoints (default: h256)
template <class CheckpointStore, class Key, class Value, class CheckpointName = bcos::h256>
concept CheckpointStorage =
    requires(CheckpointStore& store, CheckpointName name, decltype(store.open()) latestStorage) {
        // open() returns a fully readable+writable storage for the latest state
        { store.open() } -> ReadWriteStorage<Key, Value>;
        // open(name) returns a read-only storage for a historical checkpoint
        { store.open(name) } -> ReadableStorage<Key>;
        // createCheckpoint snapshots the given live storage under a name
        store.createCheckpoint(latestStorage, name);
        // deleteCheckpoint removes a named checkpoint
        store.deleteCheckpoint(name);
        // latestCheckpointName returns the most recent checkpoint, if any
        { store.latestCheckpointName() } -> std::convertible_to<std::optional<CheckpointName>>;
        // oldestCheckpointName returns the earliest checkpoint, if any
        { store.oldestCheckpointName() } -> std::convertible_to<std::optional<CheckpointName>>;
    };

}  // namespace bcos::storage2
