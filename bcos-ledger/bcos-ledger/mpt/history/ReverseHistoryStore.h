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
 * @file ReverseHistoryStore.h
 * @brief One block's diff, stored twice under two sort orders, so that both a point query and a
 *        whole-block sweep are one seek (spec B.1-B.8)
 */
#pragma once

#include "../Errors.h"
#include "HistoryErrors.h"
#include "HistoryRowCodec.h"
#include "HistoryTables.h"
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <variant>
#include <vector>

namespace bcos::ledger::mpt::history
{

/// A storage the component writes history rows and expiry deletions into. In production this is
/// the block's mutable layer, so those rows ride the block's single WriteBatch (G3).
template <class Storage>
concept WritableStateStorage = requires(Storage& storage,
    std::vector<std::tuple<executor_v1::StateKey, executor_v1::StateValue>> keyValues,
    std::vector<executor_v1::StateKey> keys) {
    { storage2::writeSome(storage, keyValues) } -> task::IsAwaitable;
    { storage2::removeSome(storage, keys) } -> task::IsAwaitable;
};

/// A storage that can position an iterator at the first row at or after a key and walk forward.
/// Both readAt and the manifest sweep need it; a plain point-read storage cannot serve either.
template <class Storage>
concept SeekableStateStorage = requires(Storage& storage, executor_v1::StateKey key) {
    { storage2::range(storage, storage2::RANGE_SEEK, key) } -> task::IsAwaitable;
};

/// One key changed by one block, paired with the value it held when the block began.
/// Both fields are non-owning views into the caller's diff — put() copies out of them.
struct HistoryEntry
{
    std::span<const bcos::byte> key;
    /// nullopt when the key did not exist before this block (written as tag 0x00).
    std::optional<std::span<const bcos::byte>> oldValue;
};

/// readAt outcome: the key did not exist at the queried block (the index row's tag is 0x00).
struct HistoryAbsent
{
    friend bool operator==(HistoryAbsent, HistoryAbsent) noexcept { return true; }
};

/// readAt outcome: no index row exists after the queried block, so the key has not changed since
/// — the current value IS the historical one and the caller should read it from the live plane
/// (spec B.3). Only trustworthy because the window guard already ruled out "the row existed and
/// was expired".
struct HistoryUseCurrent
{
    friend bool operator==(HistoryUseCurrent, HistoryUseCurrent) noexcept { return true; }
};

/// readAt outcome, third case: the key's value at the queried block, as `bcos::bytes`.
using ReadAtResult = std::variant<bcos::bytes, HistoryAbsent, HistoryUseCurrent>;

/// What one expire() pass did.
struct ExpireReport
{
    /// Keys the block's manifest listed. Zero also means "already expired" — expire is idempotent.
    std::size_t keyCount{};
    /// Index-row deletes issued, one per listed key. On a replay of an interrupted expiry some of
    /// them target rows that are already gone, which is a no-op — hence "issued", not "removed".
    std::size_t indexDeletesIssued{};
    /// Manifest shards found and deleted. These did exist: the sweep read them.
    std::size_t manifestShardsDeleted{};
};

namespace detail
{
/// Storage iterators hand back either a `StorageValueType<Value>` variant (MemoryStorage,
/// RocksDBStorage2) or a bare value. Reduce both to "the entry, or nullptr when this row carries
/// a deletion sentinel instead of bytes".
template <class RowValue>
inline const executor_v1::StateValue* asStateValue(RowValue const& value) noexcept
{
    if constexpr (requires { std::get_if<executor_v1::StateValue>(std::addressof(value)); })
    {
        return std::get_if<executor_v1::StateValue>(std::addressof(value));
    }
    else
    {
        return std::addressof(value);
    }
}
}  // namespace detail

/// The reverse history of one key space: every key a block changed, keyed both by (key, block)
/// for point queries and by (block, shard) for whole-block sweeps (spec B.1).
///
/// Instantiated twice over the same code — `StateHistoryStore` for state rows, `TrieHistoryStore`
/// for path-addressed trie nodes (spec B.8). @p Tables selects which pair of state tables the
/// instance owns; everything else is identical, which is the point.
///
/// All members are static: the store owns no state of its own. The rows live in whatever storage
/// the caller passes, and each call takes the storage it should act on — in production the
/// block's mutable layer for writes and the backend view for reads.
template <HistoryTables const& Tables>
class ReverseHistoryStore
{
public:
    /// Record @p entries as the pre-images of block @p block, plus the manifest that lists them.
    ///
    /// Pure append: every row is a fresh Put and nothing is read first, so write amplification is
    /// the size of this block's diff and does not grow with the retention depth (spec B.4). All
    /// rows go out in ONE writeSome, which in production is one contribution to the block's
    /// single WriteBatch (G3).
    ///
    /// @param entries one record per key the block changed, each carrying the value the key held
    ///        when the block BEGAN. A key must appear at most once: a second row for the same
    ///        (key, block) would overwrite the block-start value with a mid-block one and hand
    ///        every later query a wrong answer, so a duplicate throws instead of being ignored.
    ///        The caller does the intra-block deduplication (spec B.4).
    /// @param shardByteCap the manifest payload size at which a new shard starts. A key whose own
    ///        record exceeds the cap still gets a shard — the cap bounds row size, it cannot
    ///        split a record.
    ///
    /// A block that changed nothing still gets an empty shard 0. Spec B.10 ② audits the window
    /// for a manifest per block and treats a gap as fatal, so "no changes" must be recorded as
    /// such rather than being indistinguishable from a lost manifest.
    template <WritableStateStorage Storage>
    static task::Task<void> put(Storage& mutableLayer, protocol::BlockNumber block,
        std::span<HistoryEntry const> entries, std::size_t shardByteCap)
    {
        if (shardByteCap == 0)
        {
            BOOST_THROW_EXCEPTION(MPTInvariantViolation() << bcos::errinfo_comment(
                                      "history manifest shard byte cap must be positive"));
        }

        std::vector<std::tuple<executor_v1::StateKey, executor_v1::StateValue>> rows;
        rows.reserve(entries.size() + 1);

        std::unordered_set<std::string_view> seenKeys;
        seenKeys.reserve(entries.size());

        std::string shardPayload;
        std::size_t shardIndex = 0;
        auto flushShard = [&]() {
            rows.emplace_back(
                executor_v1::StateKey{Tables.manifest, manifestRowKey(block, shardIndex)},
                executor_v1::StateValue{std::move(shardPayload)});
            shardPayload.clear();
            ++shardIndex;
        };

        for (auto const& entry : entries)
        {
            auto keyView = asStringView(entry.key);
            if (!seenKeys.insert(keyView).second)
            {
                BOOST_THROW_EXCEPTION(
                    MPTInvariantViolation() << bcos::errinfo_comment(
                        "the same key appears twice in one block's history entries; only the "
                        "block-start value may be recorded"));
            }

            rows.emplace_back(executor_v1::StateKey{Tables.index, indexRowKey(entry.key, block)},
                executor_v1::StateValue{indexRowValue(entry.oldValue)});

            if (!shardPayload.empty() &&
                shardPayload.size() + manifestRecordSize(entry.key) > shardByteCap)
            {
                flushShard();
            }
            appendManifestRecord(shardPayload, entry.key);
        }
        flushShard();

        co_await storage2::writeSome(mutableLayer, std::move(rows));
    }

    /// The value @p key held at block @p block.
    ///
    /// The answer is the OLD value recorded by the first change after @p block: between @p block
    /// and that change the key was untouched, so that old value is exactly the block-@p block
    /// value (spec §0.4). One seek, one row read, independent of how far back @p block is.
    ///
    /// @param tip the chain's current block number.
    /// @param depth the retention window, i.e. H_state or H_proof for this instance.
    /// @throws HistoryPruned when @p block predates the retained window.
    /// @throws InvalidHistoryBlock when @p block is negative or ahead of @p tip.
    template <SeekableStateStorage Storage>
    static task::Task<ReadAtResult> readAt(Storage& backend, std::span<const bcos::byte> key,
        protocol::BlockNumber block, protocol::BlockNumber tip, protocol::BlockNumber depth)
    {
        // The window guard runs BEFORE the seek, and that order is the whole point (spec B.3,
        // G5): a seek that finds nothing cannot tell "never changed after B, so the current
        // value is the answer" from "the record was expired away, so the current value is a
        // wrong answer" — the index holds no evidence either way. Only the window bound
        // separates them. HISTORY_GUARD_DISABLED exists solely so the test suite can compile a
        // build without the guard and demonstrate that the out-of-window case then returns a
        // plausible-looking wrong value; nothing defines it.
#ifndef HISTORY_GUARD_DISABLED
        checkWindow(block, tip, depth);
#endif

        auto keyView = asStringView(key);
        executor_v1::StateKey seekKey{Tables.index, indexRowKey(key, block + 1)};

        auto iterator = co_await storage2::range(backend, storage2::RANGE_SEEK, seekKey);
        auto row = co_await iterator.next();
        if (!row)
        {
            co_return HistoryUseCurrent{};
        }

        auto const& [rowKey, rowValue] = *row;
        executor_v1::StateKeyView rowKeyView{rowKey};
        if (rowKeyView.m_table != Tables.index || !indexRowBelongsTo(rowKeyView.m_key, keyView))
        {
            co_return HistoryUseCurrent{};
        }

        auto const* entry = detail::asStateValue(rowValue);
        if (entry == nullptr)
        {
            // The row is present but carries a deletion sentinel rather than bytes: the
            // pre-image this query needs was removed while the window still claims to cover it.
            // Fail loud (G6) — falling through to the current value is the silent wrong answer
            // B.3 exists to prevent.
            BOOST_THROW_EXCEPTION(
                MPTInvariantViolation() << bcos::errinfo_comment(
                    "history index row is deleted inside the retention window; the pre-image "
                    "chain has a hole"));
        }
        co_return decodeIndexValue(entry->get());
    }

    /// Every key block @p block changed, read off that block's manifest. Used by rollback and by
    /// the B.10 audits, both of which need the list to be COMPLETE.
    ///
    /// Empty when the block changed nothing, and empty on a plane where the block's manifest rows
    /// are physically gone — the backend after a merge, for instance.
    ///
    /// @throws MPTInvariantViolation when a shard row is present but carries a deletion sentinel
    ///         rather than bytes. That is what an expiry looks like on the mutable layer, whose
    ///         removeSome marks rather than erases (MemoryStorage LOGICAL_DELETION, the mode
    ///         GlobalStateMutableStorage runs in). The keys such a shard listed are unreadable, so
    ///         a caller that needs the complete list must not be handed a short one (G6). Callers
    ///         wanting the expiry-tolerant reading use expire(), which skips those shards.
    template <SeekableStateStorage Storage>
    static task::Task<std::vector<bcos::bytes>> keysOfBlock(
        Storage& backend, protocol::BlockNumber block)
    {
        auto scan = co_await scanManifest(backend, block, DeletedShardPolicy::Reject);
        co_return std::move(scan.keys);
    }

    /// Drop block @p block from the retained window: delete its index rows, then its manifest.
    ///
    /// Deletions are written to @p mutableLayer, so in the default mode they land in the
    /// committing block's WriteBatch and the whole expiry is atomic with the commit (spec B.5).
    ///
    /// The manifest is deleted LAST, and that ordering is what makes the deferred mode (expiry
    /// split out of the commit batch) safe to interrupt: a crash after some index rows are gone
    /// leaves the manifest, so re-running finds the same key list and re-issues deletes that are
    /// no-ops for the rows already gone. Deleting the manifest first would strand the surviving
    /// index rows permanently — nothing else records which keys the block touched.
    ///
    /// Idempotent on every storage, including the one G3 puts it on. The production mutable layer
    /// is MemoryStorage with LOGICAL_DELETION (GlobalStateStorageInitializer.h:15-19), where a
    /// removed row stays in the container as a deletion sentinel and the iterator still yields
    /// it; so a second expire() of the same block sees its own shards coming back as sentinels.
    /// It skips them instead of failing, and that is sound rather than merely convenient: the
    /// index-then-manifest order above means a shard can only be deleted after the index rows of
    /// every key it listed were deleted, so a skipped shard has nothing left to clean up.
    /// keysOfBlock() takes the opposite reading of the same row, because a short key list would
    /// silently break rollback.
    template <SeekableStateStorage ReadStorage, WritableStateStorage WriteStorage>
    static task::Task<ExpireReport> expire(
        ReadStorage& backend, WriteStorage& mutableLayer, protocol::BlockNumber block)
    {
        auto scan = co_await scanManifest(backend, block, DeletedShardPolicy::Skip);

        ExpireReport report{.keyCount = scan.keys.size(),
            .indexDeletesIssued = scan.keys.size(),
            .manifestShardsDeleted = scan.manifestKeys.size()};

        if (!scan.keys.empty())
        {
            std::vector<executor_v1::StateKey> indexKeys;
            indexKeys.reserve(scan.keys.size());
            for (auto const& key : scan.keys)
            {
                indexKeys.emplace_back(Tables.index, indexRowKey(key, block));
            }
            co_await storage2::removeSome(mutableLayer, std::move(indexKeys));
        }
        if (!scan.manifestKeys.empty())
        {
            co_await storage2::removeSome(mutableLayer, std::move(scan.manifestKeys));
        }
        co_return report;
    }

private:
    /// One manifest sweep: the keys the block changed and the manifest rows that list them.
    struct ManifestScan
    {
        std::vector<bcos::bytes> keys;
        std::vector<executor_v1::StateKey> manifestKeys;
    };

    /// What a manifest sweep does with a shard row that is present but carries a deletion
    /// sentinel instead of bytes — the shape an expired shard has on a logical-deletion layer.
    /// The row is the same; the two callers need opposite readings of it, so the choice is a
    /// parameter rather than a rule baked into the sweep.
    enum class DeletedShardPolicy : uint8_t
    {
        /// The caller needs the block's complete key list (rollback, the B.10 audits). A deleted
        /// shard makes part of that list unreadable, so fail loud instead of returning a short
        /// one (G6).
        Reject,
        /// The caller only re-issues deletes (expire). A shard is deleted only after the index
        /// rows of every key it listed were deleted — B.5's index-then-manifest order — so a
        /// deleted shard has nothing left to clean up and skipping it is what makes a replay a
        /// no-op rather than an error.
        Skip,
    };

    /// spec B.3, first two lines. Kept as its own function so the one call site above can be
    /// compiled out wholesale for the negative-control test.
    static void checkWindow(
        protocol::BlockNumber block, protocol::BlockNumber tip, protocol::BlockNumber depth)
    {
        if (depth < 0)
        {
            BOOST_THROW_EXCEPTION(InvalidHistoryBlock() << bcos::errinfo_comment(
                                      "history retention depth must not be negative"));
        }
        if (block < 0)
        {
            BOOST_THROW_EXCEPTION(InvalidHistoryBlock() << bcos::errinfo_comment(
                                      "history block number must not be negative"));
        }
        // Signed arithmetic on purpose: a chain shorter than the window makes tip - depth + 1
        // negative, which correctly admits every block down to genesis.
        if (block < tip - depth + 1)
        {
            BOOST_THROW_EXCEPTION(HistoryPruned() << bcos::errinfo_comment(
                                      "requested block is older than the retained history "
                                      "window"));
        }
        if (block > tip)
        {
            BOOST_THROW_EXCEPTION(InvalidHistoryBlock() << bcos::errinfo_comment(
                                      "requested block is ahead of the chain tip"));
        }
    }

    /// Decode an index row value: tag byte, then the old value when the tag says there is one.
    static ReadAtResult decodeIndexValue(std::string_view value)
    {
        if (value.empty())
        {
            BOOST_THROW_EXCEPTION(MPTInvariantViolation() << bcos::errinfo_comment(
                                      "history index row value is empty; the tag byte is "
                                      "mandatory"));
        }
        switch (value.front())
        {
        case kTagAbsent:
            if (value.size() != kTagBytes)
            {
                BOOST_THROW_EXCEPTION(
                    MPTInvariantViolation() << bcos::errinfo_comment(
                        "history index row is tagged ABSENT but carries value bytes"));
            }
            return HistoryAbsent{};
        case kTagValue:
        {
            auto payload = value.substr(kTagBytes);
            return bcos::bytes(payload.begin(), payload.end());
        }
        default:
            BOOST_THROW_EXCEPTION(MPTInvariantViolation() << bcos::errinfo_comment(
                                      "history index row carries an unknown tag byte"));
        }
    }

    /// Walk the manifest shards of one block, in shard order, collecting both the keys they list
    /// and the row keys of the shards themselves. Stops at the first row that is not a manifest
    /// row of this block — the iterator runs on into the rest of the table and then into other
    /// tables, so the loop, not the seek, defines the range. A shard carrying a deletion sentinel
    /// is not such a boundary: it is skipped or rejected per @p deletedShards, and the walk goes
    /// on either way, because live shards can follow a deleted one.
    template <SeekableStateStorage Storage>
    static task::Task<ManifestScan> scanManifest(
        Storage& backend, protocol::BlockNumber block, DeletedShardPolicy deletedShards)
    {
        ManifestScan scan;
        executor_v1::StateKey seekKey{Tables.manifest, manifestRowKey(block, 0)};

        auto iterator = co_await storage2::range(backend, storage2::RANGE_SEEK, seekKey);
        while (true)
        {
            auto row = co_await iterator.next();
            if (!row)
            {
                break;
            }
            auto const& [rowKey, rowValue] = *row;
            executor_v1::StateKeyView rowKeyView{rowKey};
            if (rowKeyView.m_table != Tables.manifest ||
                !manifestRowBelongsTo(rowKeyView.m_key, block))
            {
                break;
            }
            auto const* entry = detail::asStateValue(rowValue);
            if (entry == nullptr)
            {
                if (deletedShards == DeletedShardPolicy::Skip)
                {
                    continue;
                }
                BOOST_THROW_EXCEPTION(
                    MPTInvariantViolation() << bcos::errinfo_comment(
                        "history manifest shard is deleted; the keys it listed cannot be "
                        "recovered from this storage layer"));
            }
            scan.manifestKeys.emplace_back(rowKeyView);
            for (auto& key : decodeManifestShard(entry->get()))
            {
                scan.keys.emplace_back(std::move(key));
            }
        }
        co_return scan;
    }
};

/// The two instantiations spec B.8 calls for.
using StateHistoryStore = ReverseHistoryStore<kStateHistory>;
using TrieHistoryStore = ReverseHistoryStore<kTrieHistory>;

}  // namespace bcos::ledger::mpt::history
