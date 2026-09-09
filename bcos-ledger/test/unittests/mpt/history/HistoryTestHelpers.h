/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file HistoryTestHelpers.h
 * @brief Shared fixtures for the reverse-history suites: seek-capable backends, a diff builder
 *        that keeps HistoryEntry's views alive, and a raw row dump for asserting on the layout
 */
#pragma once

#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/mpt/history/ReverseHistoryStore.h>
#include <bcos-task/Task.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <cstddef>
#include <deque>
#include <memory>
#include <optional>
// CountingStorage below constrains its forwarders on ::ranges::input_range; MemoryStorage.h does
// not promise to pull the concept in.
#include <range/v3/range/concepts.hpp>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace bcos::ledger::mpt::history::test
{

/// The unit backend. ORDERED (and therefore single-bucket, non-concurrent) is what makes
/// MemoryStorage::range(RANGE_SEEK, key) usable: the seek positions inside one bucket's ordered
/// index, so it is only a whole-storage seek when there is exactly one bucket
/// (MemoryStorage.h:547 seek + MemoryStorage.h:97 getBucketSize).
/// Rows are compared by (table, rowKey), which inside one table is the same byte order RocksDB
/// applies to "<table>:<rowKey>" — the order the meta-before-shard-0 layout stands on.
using HistoryMemStorage = bcos::storage2::memory_storage::MemoryStorage<bcos::executor_v1::StateKey,
    bcos::storage::Entry, bcos::storage2::memory_storage::ORDERED>;

/// Same backend with logical deletion turned on, so a removed row stays visible to the iterator as
/// a deletion sentinel instead of vanishing — the shape a mutable layer has before it is merged
/// down.
using HistoryLogicalDeleteStorage =
    bcos::storage2::memory_storage::MemoryStorage<bcos::executor_v1::StateKey, bcos::storage::Entry,
        bcos::storage2::memory_storage::ORDERED | bcos::storage2::memory_storage::LOGICAL_DELETION>;

/// Backend wrapper that counts the calls it is asked to serve. put() must never read or seek, and
/// expire() must delete whole rows rather than one key at a time — both are counted here.
struct CountingStorage
{
    HistoryMemStorage inner;
    std::size_t readCalls{};
    std::size_t rangeCalls{};
    std::size_t removeCalls{};
    std::size_t removedKeys{};

    auto writeSome(::ranges::input_range auto keyValues)
    {
        return inner.writeSome(std::move(keyValues));
    }
    auto removeSome(::ranges::input_range auto keys, auto&&... args)
    {
        ++removeCalls;
        removedKeys += static_cast<std::size_t>(::ranges::size(keys));
        return inner.removeSome(std::move(keys), std::forward<decltype(args)>(args)...);
    }
    auto readOne(auto key, auto&&... args)
    {
        ++readCalls;
        return inner.readOne(std::move(key), std::forward<decltype(args)>(args)...);
    }
    auto readSome(::ranges::input_range auto keys, auto&&... args)
    {
        ++readCalls;
        return inner.readSome(std::move(keys), std::forward<decltype(args)>(args)...);
    }
    auto range(auto&&... args)
    {
        ++rangeCalls;
        return inner.range(std::forward<decltype(args)>(args)...);
    }
};

/// Bytes of a printable test key or value.
inline bcos::bytes makeBytes(std::string_view text)
{
    return {text.begin(), text.end()};
}

/// A block hash that is distinguishable per block, so a meta row read back can be attributed.
inline bcos::h256 blockHashOf(bcos::protocol::BlockNumber block)
{
    bcos::h256 hash;
    hash.mutableData()[30] = static_cast<bcos::byte>((block >> 8) & 0xFF);
    hash.mutableData()[31] = static_cast<bcos::byte>(block & 0xFF);
    return hash;
}

/// Owns the key and value bytes a batch of HistoryEntry views point at. HistoryEntry is
/// deliberately non-owning (put copies out of it), so the test needs somewhere stable to keep them;
/// std::deque is used because push_back leaves references to existing elements valid.
class Diff
{
public:
    Diff() = default;
    // Neither copyable nor movable on purpose: m_entries holds views into m_owned, and any
    // relocation of the Diff would leave those views pointing at the moved-from deque. Deleting
    // the operations turns `auto d = Diff{}.change(...)` — which would silently dangle — into a
    // compile error.
    Diff(const Diff&) = delete;
    Diff(Diff&&) = delete;
    Diff& operator=(const Diff&) = delete;
    Diff& operator=(Diff&&) = delete;
    ~Diff() = default;

    /// Record that @p key changed in this block, having held @p oldValue when the block began.
    /// @p oldValue nullopt means the key did not exist yet.
    Diff& change(std::string_view key, std::optional<std::string_view> oldValue)
    {
        auto const& keyBytes = m_owned.emplace_back(makeBytes(key));
        std::optional<std::span<const bcos::byte>> valueView;
        if (oldValue)
        {
            valueView = std::span<const bcos::byte>(m_owned.emplace_back(makeBytes(*oldValue)));
        }
        m_entries.push_back(HistoryEntry{.key = keyBytes, .oldValue = valueView});
        return *this;
    }

    [[nodiscard]] std::span<HistoryEntry const> entries() const { return m_entries; }

private:
    std::deque<bcos::bytes> m_owned;
    std::vector<HistoryEntry> m_entries;
};

/// Every row of @p table currently in @p storage, as (rowKey, value) in ascending key order.
/// The suites assert on this directly: the layout is a byte-exact promise (layout spec §1.2), so
/// the tests read the bytes rather than only the accessors that produced them. Deletion sentinels
/// are skipped, so on a logical-deletion backend this shows what is still LIVE.
inline std::vector<std::pair<std::string, std::string>> rowsOfTable(
    auto& storage, std::string_view table)
{
    return bcos::task::syncWait(
        [&]() -> bcos::task::Task<std::vector<std::pair<std::string, std::string>>> {
            std::vector<std::pair<std::string, std::string>> rows;
            auto iterator = co_await bcos::storage2::range(storage);
            while (true)
            {
                auto row = co_await iterator.next();
                if (!row)
                {
                    break;
                }
                auto const& [rowKey, rowValue] = *row;
                bcos::executor_v1::StateKeyView view{rowKey};
                if (view.m_table != table)
                {
                    continue;
                }
                auto const* entry = std::get_if<bcos::storage::Entry>(std::addressof(rowValue));
                if (entry == nullptr)
                {
                    continue;
                }
                rows.emplace_back(std::string(view.m_key), std::string(entry->get()));
            }
            co_return rows;
        }());
}

/// A shard cap large enough that the cases which are not about sharding never split.
inline constexpr std::size_t kWideShardCap = 64 * 1024;

// The suites share these wrappers rather than each defining its own: the ledger test binary is a
// unity build, so per-file anonymous namespaces would land in one translation unit and collide.

/// Write one block's history rows and return the index update they imply, WITHOUT applying it —
/// the state the commit path is in between staging and a successful merge (G9).
inline StagedBlock stageBlock(auto& store, auto& storage, bcos::protocol::BlockNumber block,
    Diff const& diff, std::size_t shardCap = kWideShardCap)
{
    return bcos::task::syncWait(
        store.put(storage, block, blockHashOf(block), diff.entries(), shardCap));
}

/// Write one block's history and publish it, i.e. the whole commit path for a block that expires
/// nothing.
inline void putBlock(auto& store, auto& storage, bcos::protocol::BlockNumber block,
    Diff const& diff, std::size_t shardCap = kWideShardCap)
{
    store.publish(stageBlock(store, storage, block, diff, shardCap), std::nullopt, std::nullopt);
}

/// Query the history for @p key at @p block, against a chain at @p tip retaining @p depth.
inline ReadAtResult readAt(auto& store, auto& storage, std::string_view key,
    bcos::protocol::BlockNumber block, bcos::protocol::BlockNumber tip,
    bcos::protocol::BlockNumber depth)
{
    auto keyBytes = makeBytes(key);
    return bcos::task::syncWait(store.readAt(storage, keyBytes, block, tip, depth));
}

/// One block's whole recorded diff.
inline BlockHistory readBlock(auto& store, auto& storage, bcos::protocol::BlockNumber block)
{
    return bcos::task::syncWait(store.readBlock(storage, block));
}

/// Expire one block, writing the deletions into the same storage they are read from — the
/// deferred-mode shape, where a replay must converge on the same final state. The retention
/// boundary is left alone (expire's default), which is what a caller discarding from the TOP — an
/// operational rollback — needs.
inline ExpireReport expireBlock(auto& store, auto& storage, bcos::protocol::BlockNumber block)
{
    return bcos::task::syncWait(store.expire(storage, storage, block));
}

/// The commit path's flavour of the same call: the expired block is the OLDEST one still recorded,
/// so the retention boundary advances to it in the same batch.
inline ExpireReport expireBlockAdvancing(
    auto& store, auto& storage, bcos::protocol::BlockNumber block)
{
    return bcos::task::syncWait(store.expire(storage, storage, block, RetentionBoundary::Advance));
}

/// The retention boundary as it is on disk.
inline std::optional<bcos::protocol::BlockNumber> boundaryOnDisk(auto& store, auto& storage)
{
    return bcos::task::syncWait(store.retentionBoundary(storage));
}

/// Bytes as text, for readable assertions.
inline std::string toText(bcos::bytes const& value)
{
    return {value.begin(), value.end()};
}

/// One row's value, as an owned copy. Owned on purpose: `readOne` hands back an optional whose
/// lifetime ends with the full expression, so a string_view into it would dangle in the caller.
/// @throws std::bad_optional_access when the row is absent.
inline std::string readRowValue(auto& storage, std::string_view table, std::string const& rowKey)
{
    auto const row = bcos::task::syncWait(
        bcos::storage2::readOne(storage, bcos::executor_v1::StateKey{table, rowKey}));
    return std::string(row.value().get());
}

/// Overwrite one row behind the store's back, to stand in for on-disk corruption.
inline void overwriteRow(
    auto& storage, std::string_view table, std::string const& rowKey, std::string value)
{
    bcos::task::syncWait(
        bcos::storage2::writeOne(storage, bcos::executor_v1::StateKey{table, rowKey},
            bcos::executor_v1::StateValue{std::move(value)}));
}

/// Delete one row behind the store's back.
inline void deleteRow(auto& storage, std::string_view table, std::string const& rowKey)
{
    bcos::task::syncWait(
        bcos::storage2::removeOne(storage, bcos::executor_v1::StateKey{table, rowKey}));
}

}  // namespace bcos::ledger::mpt::history::test
