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
 * @file HistoryCommit.h
 * @brief The commit-time half of the two reverse histories: capture one block's pre-images into
 *        that block's own WriteBatch and drop the blocks that just left the two windows
 *        (spec §9, §10, §12, appendix B.4/B.5)
 */
#pragma once

#include "../Classify.h"
#include "../PathKey.h"
#include "HistoryDepths.h"
#include "ReverseHistoryStore.h"
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

namespace bcos::ledger::mpt::history
{

/// The manifest payload size at which a new shard starts (spec B.9).
///
/// B.9 sizes the manifest but does not fix a number, so this is a judgement, recorded here rather
/// than at a call site: 64 KiB is well under RocksDB's blob threshold and its default 64 MiB
/// memtable, so a shard is an ordinary row that never forces a large-value path; and at the ~40
/// bytes an account row key takes (4-byte length + "/apps/" + 40 hex + ':' + field name) one shard
/// still lists on the order of a thousand keys, so a normal block's manifest is ONE row and the
/// per-row overhead of sharding is not paid at all. A block big enough to need several shards is
/// exactly the block for which a single multi-megabyte row would hurt.
inline constexpr std::size_t kManifestShardByteCap = 64UL * 1024UL;

/// The index key a state row is recorded under: the row's PHYSICAL key bytes, `"<table>:<rowKey>"`
/// — the exact string a StateKey already holds, so no second key format exists to drift.
///
/// Using the physical form rather than (table, rowKey) is what keeps the two trie-node tables from
/// colliding: "/mptp/a" and "/mptp/s" are distinct prefixes, and neither table name contains ':'
/// (PathKey.h's static_asserts), so the mapping is injective for node rows and for flat rows alike.
[[nodiscard]] inline std::span<const bcos::byte> historyKeyOf(
    executor_v1::StateKey const& key) noexcept
{
    return {reinterpret_cast<bcos::byte const*>(key.data()), key.size()};
}

/// Which flat rows the state history captures: the four row kinds inside an account table that
/// MPTBuilder folds into the Ethereum state commitment — exactly the set `accumulateRow` flags as
/// `sawEthereumRow` (MPTBuilder.h). Classification is `classifyRowKey`'s, not a second parser.
///
/// The complement is deliberate and matches what the historical read plane already served from the
/// MPT before this wiring existed (HistoricalCallStorage.h's class comment): `code` is represented
/// by its paired `codeHash` and lives content-addressed in s_code_binary, the KNOWN BCOS extension
/// fields are outside the committed four-tuple, and system / BFS / ledger tables have no per-block
/// commitment at all. Rows outside the set are answered from the latest plane, as they were.
[[nodiscard]] inline bool isHistoricalStateRow(executor_v1::StateKeyView const& keyView)
{
    if (!parseAccountTable(keyView.m_table))
    {
        return false;
    }
    switch (classifyRowKey(keyView.m_key))
    {
    case RowKind::Nonce:
    case RowKind::Balance:
    case RowKind::CodeHash:
    case RowKind::StorageSlot:
        return true;
    case RowKind::Code:
    case RowKind::BcosExtension:
    case RowKind::UnknownField:
        return false;
    }
    return false;
}

/// The keys of the flat state rows @p layer holds, filtered to what the state history captures.
///
/// @p layer must be the block's OWN mutable layer, not a view over it: the point is to enumerate
/// what THIS block changed, and a view would also yield every row underneath. Called at EXECUTE
/// time, because that is the last moment the layer is reachable — coExecuteBlock hands it to the
/// storage stack with pushView, and commit only ever sees it again inside mergeBackStorage.
///
/// Logically deleted rows are included: the block removed them, so their pre-image is exactly what
/// a query at an earlier height needs.
template <class Layer>
[[nodiscard]] task::Task<std::vector<executor_v1::StateKey>> collectStateHistoryKeys(Layer& layer)
{
    std::vector<executor_v1::StateKey> keys;
    auto iterator = co_await storage2::range(layer);
    while (true)
    {
        auto row = co_await iterator.next();
        if (!row)
        {
            break;
        }
        executor_v1::StateKeyView const keyView{std::get<0>(*row)};
        if (isHistoricalStateRow(keyView))
        {
            keys.emplace_back(keyView);
        }
    }
    co_return keys;
}

/// Seed @p Store's retention boundary with @p block when @p current says it has none.
///
/// A store's boundary is the oldest block it can still ANSWER FOR, and the commit-time expiry
/// advances it. The first block a store ever records has nothing to expire, so it has to seed it
/// — and it must, because a query reads an ABSENT boundary as "this store recorded nothing" and
/// refuses.
///
/// The seed is @p block - 1, not @p block: a query for B is answered by the first change AFTER
/// B, so the first recorded block's own pre-images are exactly what answers the block before it.
/// One lower than that is not answerable — a change in the unrecorded era would be invisible and
/// the seek would return a later block's pre-image as if nothing had happened in between.
template <class Store, WritableStateStorage Batch>
task::Task<void> seedRetentionBoundary(
    Batch& batch, std::optional<protocol::BlockNumber> current, protocol::BlockNumber block)
{
    if (current)
    {
        co_return;
    }
    co_await Store::writeRetentionBoundary(batch, std::max<protocol::BlockNumber>(0, block - 1));
}

/// Whether this commit's expiry of @p expiring may move the retention boundary, given the value
/// the boundary will hold once this batch's seed (if any) has landed.
///
/// Normally the answer is yes: at commit time @p expiring is the oldest block leaving the
/// window, which is exactly what the boundary records. Three cases say no, and all three would
/// otherwise move the boundary DOWN — claiming heights are intact whose index rows are already
/// gone, the same silent wrong answer this metadata exists to prevent, reached from the other
/// side:
///
///  - **mid-chain activation.** History enabled at block N0 seeds N0 - 1, and the SAME commit
///    expires N0 - H, which for H > N0 is far below it. Nothing under N0 - 1 was ever recorded.
///  - **a raised depth.** An operator raising mpt_history_*_blocks across a restart makes
///    `block - H` jump backwards on the next commit, below a boundary an earlier commit set.
///  - **a rollback.** The chain re-commits from a lower height, so `block - H` lands below the
///    boundary the pre-rollback tip established.
///
/// @p afterSeed is the post-seed value on purpose: the seed is written into the same batch as
/// this expiry, so comparing against the pre-seed backend value would let the activation case
/// through (the seed is not visible in the backend yet).
inline RetentionBoundary expiryBoundaryPolicy(
    protocol::BlockNumber afterSeed, protocol::BlockNumber expiring)
{
    return afterSeed < expiring ? RetentionBoundary::Advance : RetentionBoundary::Keep;
}

/// The value a store's boundary holds once this block's seed has landed: the existing row, or
/// the seed this commit is about to write for a store that has none.
inline protocol::BlockNumber boundaryAfterSeed(
    std::optional<protocol::BlockNumber> current, protocol::BlockNumber block)
{
    return current ? *current : std::max<protocol::BlockNumber>(0, block - 1);
}

/// What one commitBlockHistory() pass did — reported so the caller can log it and so a test can
/// assert the block's own numbers instead of re-deriving them from the rows.
struct HistoryCommitReport
{
    /// Flat rows recorded in this block's state history (also the number of backend reads the
    /// capture cost — one readSome over exactly these keys).
    std::size_t stateEntries{};
    /// Trie positions recorded in this block's node history: PathDiff::preimages.size().
    std::size_t trieEntries{};
    ExpireReport stateExpired{};
    ExpireReport trieExpired{};
};

/// Record block @p block's two reverse histories into @p batch and expire the two blocks that just
/// left the windows. The SINGLE entry point both commit paths call — BaselineScheduler's and
/// OpScheduler's coCommitBlock differ in how they reach this point, not in the bookkeeping.
///
/// @param backend the COMMITTED plane, holding state through block @p block - 1: the pre-images
///        are read from it (spec §0.4 / code map Q3 — the commit path itself has no old values:
///        the delta rows carry only new state and mergeBackStorage never reads its destination),
///        and expire()'s manifest sweep seeks in it. Must be seekable, which the production
///        `latestBackend()` (RocksDBStorage2) is and a production `View` is not — its LRU cache
///        layer is CONCURRENT|LRU with no ORDERED, so `View::range(RANGE_SEEK, …)` cannot even be
///        instantiated.
/// @param batch a mutable layer that the caller will hand to this block's single
///        `mergeBackStorage` — in production the commit's prewrite buffer. History rows, index
///        rows and expiry deletes therefore ride the block's one WriteBatch (G3, spec §13): a
///        crash cannot leave "current state advanced, one block's history missing".
/// @param stateKeys the block's flat rows, from collectStateHistoryKeys at execute time.
/// @param triePreimages PathDiff::preimages — position → the bytes that position held before this
///        block (nullopt = nothing was there). Free of I/O: the builder already read every one of
///        them while resolving the changed paths (spec §9).
/// @param depths the node's retention window; a zero depth writes nothing for that history and
///        expires nothing.
///
/// Called ONCE per block per store — ReverseHistoryStore::put allows exactly one call per
/// (instance, block).
///
/// **Manifest coverage contract** (HistoryTables.h::kManifestCoverageContract, and what PR-D's
/// rollback and audit may assume): both commit paths call this only for a block that BUILT AN
/// MPT DELTA, and it writes to a store only while that store's depth is > 0. So a manifest
/// exists for block N in store S iff both hold. A block that qualifies but changed nothing still
/// gets an empty shard 0 (spec B.10 ②), so "no manifest" never means "changed nothing" — it
/// means the pre-images were never captured, which is true of every pre-MPT block of a
/// scenario-A chain and of every block committed while the depth was 0. Empty manifests are
/// deliberately NOT written for those: the row would assert a recording that did not happen. A
/// consumer spanning such a block must refuse rather than read the gap as an empty diff.
template <QueryableStateStorage Backend, WritableStateStorage Batch>
task::Task<HistoryCommitReport> commitBlockHistory(Backend& backend, Batch& batch,
    protocol::BlockNumber block, std::span<executor_v1::StateKey const> stateKeys,
    std::map<PathKey, std::optional<bcos::bytes>> const& triePreimages, HistoryDepths const& depths)
{
    HistoryCommitReport report;

    // Each enabled store's boundary is read ONCE, and the same value answers both questions this
    // commit has about it: does this block have to seed it, and may this block's expiry advance
    // it. One point read per enabled store per block, of a row that is hot by construction.
    std::optional<protocol::BlockNumber> stateBoundary;
    std::optional<protocol::BlockNumber> trieBoundary;
    if (depths.state > 0)
    {
        stateBoundary = co_await StateHistoryStore::retentionBoundary(backend);
    }
    if (depths.proof > 0)
    {
        trieBoundary = co_await TrieHistoryStore::retentionBoundary(backend);
    }

    if (depths.state > 0)
    {
        // The one extra read per block the state history costs: a single readSome over exactly
        // the rows the block changed. Nothing else in the commit path can supply them.
        auto oldValues = co_await storage2::readSome(backend, stateKeys);
        std::vector<HistoryEntry> entries;
        entries.reserve(stateKeys.size());
        for (std::size_t index = 0; index < stateKeys.size(); ++index)
        {
            std::optional<std::span<const bcos::byte>> oldValue;
            if (oldValues[index])
            {
                auto const raw = oldValues[index]->get();
                oldValue.emplace(reinterpret_cast<bcos::byte const*>(raw.data()), raw.size());
            }
            entries.emplace_back(
                HistoryEntry{.key = historyKeyOf(stateKeys[index]), .oldValue = oldValue});
        }
        report.stateEntries = entries.size();
        co_await StateHistoryStore::put(batch, block, entries, kManifestShardByteCap);
        co_await seedRetentionBoundary<StateHistoryStore>(batch, stateBoundary, block);
    }

    if (depths.proof > 0)
    {
        // The node rows' physical keys have to outlive the put(): HistoryEntry holds a view, not
        // a copy. reserve() is exact, so no emplace_back below can reallocate the vector the
        // spans point into.
        std::vector<executor_v1::StateKey> nodeKeys;
        nodeKeys.reserve(triePreimages.size());
        std::vector<HistoryEntry> entries;
        entries.reserve(triePreimages.size());
        for (auto const& [pathKey, prior] : triePreimages)
        {
            nodeKeys.emplace_back(pathNodeStateKey(pathKey));
            std::optional<std::span<const bcos::byte>> oldValue;
            if (prior)
            {
                oldValue.emplace(prior->data(), prior->size());
            }
            entries.emplace_back(
                HistoryEntry{.key = historyKeyOf(nodeKeys.back()), .oldValue = oldValue});
        }
        report.trieEntries = entries.size();
        co_await TrieHistoryStore::put(batch, block, entries, kManifestShardByteCap);
        co_await seedRetentionBoundary<TrieHistoryStore>(batch, trieBoundary, block);
    }

    // spec §12: the block leaving each window is expired in the SAME batch that commits the new
    // block, so "an index entry pointing at a freed shard" is not a state the disk can hold. A
    // window wider than the chain so far expires nothing — the subtraction is signed on purpose.
    //
    // This is the ONE caller discarding from the BOTTOM, where the expired block is the oldest
    // one still recorded — so it is the one that may move the boundary, and it says so
    // explicitly (expire's default is Keep, for a rollback discarding from the top). The policy
    // is still computed rather than assumed: mid-chain activation, a raised depth and a rollback
    // all make `block - H` land BELOW the boundary, and it must not follow them down
    // (expiryBoundaryPolicy). expire() takes a max of its own as well, for callers outside this
    // one.
    if (depths.state > 0)
    {
        if (auto const expiring = block - depths.state; expiring >= 0)
        {
            report.stateExpired = co_await StateHistoryStore::expire(backend, batch, expiring,
                expiryBoundaryPolicy(boundaryAfterSeed(stateBoundary, block), expiring));
        }
    }
    if (depths.proof > 0)
    {
        if (auto const expiring = block - depths.proof; expiring >= 0)
        {
            report.trieExpired = co_await TrieHistoryStore::expire(backend, batch, expiring,
                expiryBoundaryPolicy(boundaryAfterSeed(trieBoundary, block), expiring));
        }
    }

    co_return report;
}

}  // namespace bcos::ledger::mpt::history
