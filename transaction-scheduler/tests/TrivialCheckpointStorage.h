#pragma once

#include "bcos-framework/storage2/Storage.h"
#include "bcos-utilities/Exceptions.h"
#include "bcos-utilities/FixedBytes.h"
#include <optional>

namespace bcos::storage2
{

DERIVE_BCOS_EXCEPTION(TrivialCheckpointNotSupported);

/// @brief A trivial checkpoint adapter that wraps any existing ReadWriteStorage
/// and makes it satisfy the CheckpointStorage concept.
///
/// This is intended for testing and benchmarking scenarios where a full
/// checkpoint-capable storage (like CheckpointRocksDBStorage) is not needed.
///
/// open() returns a reference to the wrapped storage (no ownership transfer).
/// open(blockhash) throws — historical checkpoints are not supported.
/// createCheckpoint/deleteCheckpoint are no-ops.
///
/// @tparam Storage  The underlying storage type (must satisfy ReadWriteStorage)
template <class Key, class Value, ReadWriteStorage<Key, Value> Storage>
class TrivialCheckpointStorage
{
public:
    using CheckpointName = bcos::h256;

    explicit TrivialCheckpointStorage(Storage& storage) noexcept : m_storage(storage) {}

    Storage& open() & { return m_storage; }

    Storage& open(CheckpointName const& /*unused*/) &
    {
        BOOST_THROW_EXCEPTION(TrivialCheckpointNotSupported{} << bcos::errinfo_comment(
                                  "TrivialCheckpointStorage does not support historical "
                                  "checkpoints"));
        return m_storage;  // unreachable
    }

    void createCheckpoint(Storage& /*latestStorage*/, CheckpointName const& /*unused*/)
    {
        // no-op: trivial storage does not create real checkpoints
    }

    void deleteCheckpoint(CheckpointName const& /*unused*/)
    {
        // no-op
    }

    std::optional<CheckpointName> latestCheckpointName() const { return std::nullopt; }

    std::optional<CheckpointName> oldestCheckpointName() const { return std::nullopt; }

private:
    std::reference_wrapper<Storage> m_storage;
};

}  // namespace bcos::storage2
