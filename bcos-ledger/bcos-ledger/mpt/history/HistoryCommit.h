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
#include "MPTHistory.h"
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

/// The shard payload size at which a new shard row starts (spec B.9, layout spec §1.2).
///
/// B.9 sizes the shard but does not fix a number, so this is a judgement, recorded here rather
/// than at a call site: 64 KiB is well under RocksDB's blob threshold and its default 64 MiB
/// memtable, so a shard is an ordinary row that never forces a large-value path; and at the ~40
/// bytes an account row key takes (4-byte length + "/apps/" + 40 hex + ':' + field name) plus its
/// old value, one shard still holds hundreds of records, so a normal block's history is ONE shard
/// row and the per-row overhead of sharding is not paid at all. A block big enough to need several
/// shards is exactly the block for which a single multi-megabyte row would hurt.
inline constexpr std::size_t kHistoryShardByteCap = 64UL * 1024UL;

/// The history key a state row is recorded under: the row's PHYSICAL key bytes,
/// `"<table>:<rowKey>"`
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

/// What one stageBlockHistory() pass did — reported so the caller can log it and so a test can
/// assert the block's own numbers instead of re-deriving them from the rows.
struct HistoryCommitReport
{
    /// Flat rows recorded in this block's state history (also the number of backend reads the
    /// capture cost — one readSome over exactly these keys).
    std::size_t stateEntries{};
    /// Trie positions recorded in this block's node history: PathDiff::preimages.size().
    std::size_t trieEntries{};
    /// Counts only: the `retired` block inside each of these has been moved into the stage, which
    /// is where publishing takes it from.
    ExpireReport stateExpired{};
    ExpireReport trieExpired{};
};

/// What one store's share of stageBlockHistory() wrote to the batch, and therefore what its
/// in-memory index must learn once that batch has landed (G9).
///
/// Every field is the OUTPUT of a disk write that has not been published yet. Dropping this
/// object — which is what a scope exit after a throwing merge does — is exactly the rollback: the
/// index never hears about rows that are not there.
struct StagedStoreCommit
{
    /// The block's own records, from put(). Engaged iff this store's depth is > 0.
    std::optional<StagedBlock> staged;
    /// The block that left the window, from expire(). Engaged iff a live row was deleted.
    std::optional<RetiredBlock> retired;
    /// The retention boundary this batch wrote, if it wrote one — the seed for a store's first
    /// block, or the advanced boundary of an expiry. Applied to the index as a MAX.
    std::optional<protocol::BlockNumber> boundaryWritten;
    /// What the expiry pass did, for the caller's log line. Its own `retired` field has been
    /// moved out into `retired` above — the index's copy is the authoritative one, and leaving a
    /// second would invite a caller to publish it twice.
    ExpireReport expired;
};

/// Everything one block's history staged, across both stores, held between the write and the
/// publish. Move-only in spirit: publishBlockHistory consumes it.
struct HistoryCommitStage
{
    HistoryCommitReport report;
    StagedStoreCommit state;
    StagedStoreCommit trie;
};

/// One store's put + boundary seed + window expiry, all into @p batch, with nothing published.
///
/// Split out because the state and the trie halves differ only in where their HistoryEntry list
/// comes from; the bookkeeping around it — one boundary read, the seed rule, the expiry policy —
/// is one algorithm and is written once.
///
/// @param entries the block's pre-images. The spans inside must stay alive until this returns.
/// @param depth this store's retention window; the caller only calls with depth > 0.
template <class Store, QueryableStateStorage Backend, WritableStateStorage Batch>
task::Task<StagedStoreCommit> stageOneStore(Store const& store, Backend& backend, Batch& batch,
    protocol::BlockNumber block, bcos::h256 const& blockHash, std::span<HistoryEntry const> entries,
    protocol::BlockNumber depth)
{
    StagedStoreCommit result;

    // The boundary is read ONCE, and the same value answers both questions this commit has about
    // it: does this block have to seed it, and may this block's expiry advance it. One point read
    // per enabled store per block, of a row that is hot by construction.
    auto const boundary = co_await Store::retentionBoundary(backend);

    result.staged = co_await store.put(batch, block, blockHash, entries, kHistoryShardByteCap);
    if (!boundary)
    {
        result.boundaryWritten = std::max<protocol::BlockNumber>(0, block - 1);
    }
    co_await seedRetentionBoundary<Store>(batch, boundary, block);

    // spec §12: the block leaving the window is expired in the SAME batch that commits the new
    // block, so "an index version pointing at a freed shard" is not a state the disk can hold. A
    // window wider than the chain so far expires nothing — the subtraction is signed on purpose.
    //
    // This is the ONE caller discarding from the BOTTOM, where the expired block is the oldest one
    // still recorded — so it is the one that may move the boundary, and it says so explicitly
    // (expire's default is Keep, for a rollback discarding from the top). The policy is still
    // computed rather than assumed: mid-chain activation, a raised depth and a rollback all make
    // `block - H` land BELOW the boundary, and it must not follow them down (expiryBoundaryPolicy).
    //
    // Exactly one Advance expire per batch, with the oldest block leaving the window — which is
    // expire()'s stated precondition (ReverseHistoryStore.h): its max is computed against @p
    // backend, which cannot yet see this batch.
    if (auto const expiring = block - depth; expiring >= 0)
    {
        auto const policy = expiryBoundaryPolicy(boundaryAfterSeed(boundary, block), expiring);
        auto expired = co_await store.expire(backend, batch, expiring, policy);
        result.retired = std::move(expired.retired);
        // Moving OUT of an optional leaves it engaged holding a moved-from value — here a
        // RetiredBlock whose block number is intact and whose key list is empty. Publishing that
        // would drop the block from the index's block map while leaving its versions behind, so
        // the second copy is cleared rather than merely documented away.
        expired.retired.reset();
        result.expired = std::move(expired);
        // The same predicate expire() applies to @p backend before writing the row, evaluated on
        // the boundary value already read above rather than by reading it a second time. A seed
        // and an Advance cannot both fire for one block — the seed is `block - 1`, the expiry is
        // `block - depth <= block - 1`, and expiryBoundaryPolicy answers Keep whenever the
        // post-seed boundary is not below it — so this never overwrites a seed with a lower value.
        if (policy == RetentionBoundary::Advance && (!boundary || *boundary < expiring))
        {
            result.boundaryWritten = expiring;
        }
    }
    co_return result;
}

/// Write block @p block's two reverse histories into @p batch and expire the two blocks that just
/// left the windows — WITHOUT touching either in-memory index. The SINGLE entry point both commit
/// paths call; BaselineScheduler's and OpScheduler's coCommitBlock differ in how they reach this
/// point, not in the bookkeeping.
///
/// This is the first half of the two-phase commit G9 requires. The rows go into the block's own
/// WriteBatch here; the indexes learn about them in publishBlockHistory, which the caller invokes
/// only after that batch has landed. A merge that throws simply destroys the returned stage, and
/// the indexes are then exactly as they were before this block — no compensating action, no
/// window in which a query can see a version whose shard row does not exist.
///
/// @param backend the COMMITTED plane, holding state through block @p block - 1: the pre-images
///        are read from it (spec §0.4 / code map Q3 — the commit path itself has no old values:
///        the delta rows carry only new state and mergeBackStorage never reads its destination),
///        and expire()'s block walk seeks in it. Must be seekable, which the production
///        `latestBackend()` (RocksDBStorage2) is and a production `View` is not — its LRU cache
///        layer is CONCURRENT|LRU with no ORDERED, so `View::range(RANGE_SEEK, …)` cannot even be
///        instantiated.
/// @param batch a mutable layer that the caller will hand to this block's single
///        `mergeBackStorage` — in production the commit's prewrite buffer. History rows and expiry
///        deletes therefore ride the block's one WriteBatch (G3, spec §13): a crash cannot leave
///        "current state advanced, one block's history missing".
/// @param blockHash the committing header's hash, recorded in each store's Meta row so PR-D's
///        audit can tell which chain a retained block belongs to.
/// @param stateKeys the block's flat rows, from collectStateHistoryKeys at execute time.
/// @param triePreimages PathDiff::preimages — position → the bytes that position held before this
///        block (nullopt = nothing was there). Free of I/O: the builder already read every one of
///        them while resolving the changed paths (spec §9).
/// @param history the node's stores and depths; a zero depth writes nothing for that history and
///        expires nothing.
///
/// Called ONCE per block per store — HistoryIndex refuses a republished or out-of-order block.
///
/// **Block-history coverage contract** (HistoryTables.h::kBlockHistoryCoverageContract, and what
/// PR-D's rollback and audit may assume): both commit paths call this only for a block that BUILT
/// AN MPT DELTA, and it writes to a store only while that store's depth is > 0. So a Meta row
/// exists for block N in store S iff both hold. A block that qualifies but changed nothing still
/// gets `Meta{shardCount = 1, recordCount = 0}` and an empty shard 0 (spec B.10 ②), so "no Meta
/// row" never means "changed nothing" — it means the pre-images were never captured, which is true
/// of every pre-MPT block of a scenario-A chain and of every block committed while the depth was
/// 0. Empty records are deliberately NOT written for those: the row would assert a recording that
/// did not happen. A consumer spanning such a block must refuse rather than read the gap as an
/// empty diff.
template <QueryableStateStorage Backend, WritableStateStorage Batch>
task::Task<HistoryCommitStage> stageBlockHistory(Backend& backend, Batch& batch,
    protocol::BlockNumber block, bcos::h256 const& blockHash,
    std::span<executor_v1::StateKey const> stateKeys,
    std::map<PathKey, std::optional<bcos::bytes>> const& triePreimages, MPTHistory& history)
{
    HistoryCommitStage stage;
    auto const depths = history.depths();

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
        stage.report.stateEntries = entries.size();
        stage.state = co_await stageOneStore(
            history.state(), backend, batch, block, blockHash, entries, depths.state);
        stage.report.stateExpired = stage.state.expired;
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
        stage.report.trieEntries = entries.size();
        stage.trie = co_await stageOneStore(
            history.trie(), backend, batch, block, blockHash, entries, depths.proof);
        stage.report.trieExpired = stage.trie.expired;
    }

    co_return stage;
}

/// Hold both enabled stores' publish windows open for the stretch where DISK is ahead of the
/// INDEX (HistoryIndex.h::openPublishWindow).
///
/// Opened immediately before the merge and closed by publishBlockHistory, or by this destructor
/// on any path that does not get there. Leaving a window open would make every later
/// "unchanged since B" query retry and then refuse, so the guard exists precisely so that no
/// early return, no throw and no forgotten branch can do that.
///
/// Both stores are opened even when only one is enabled: an un-staged store's window is closed by
/// this destructor either way, and gating the open on the depth would put a second copy of "is
/// this store enabled" here.
class PublishWindow
{
public:
    explicit PublishWindow(MPTHistory& history) noexcept : m_history(std::addressof(history))
    {
        m_history->state().openPublishWindow();
        m_history->trie().openPublishWindow();
    }
    PublishWindow(PublishWindow const&) = delete;
    PublishWindow& operator=(PublishWindow const&) = delete;
    PublishWindow(PublishWindow&&) = delete;
    PublishWindow& operator=(PublishWindow&&) = delete;
    ~PublishWindow() noexcept { close(); }

    /// Idempotent: publishBlockHistory closes each store's window as it publishes it, and the
    /// destructor then finds nothing left to do.
    void close() noexcept
    {
        m_history->state().closePublishWindow();
        m_history->trie().closePublishWindow();
    }

private:
    MPTHistory* m_history;
};

/// Apply to the two in-memory indexes what @p stage already wrote to disk (G9, second half).
///
/// Call site discipline, and it is the whole point of the split: this runs AFTER the block's
/// WriteBatch has been merged, and it must be the LAST FALLIBLE STEP before the committed block
/// number advances — not merely somewhere before it.
///
/// Earlier than the merge, a failed merge leaves the index describing rows that do not exist.
/// Later than the advance, a reader admitted by the new tip would miss this block's versions and
/// read that miss as "the key never changed". And anything fallible BETWEEN this call and the
/// advance re-opens the worst case: a commit that publishes and then throws leaves the height
/// uncommitted, PBFT re-drives it, and the retry publishes block N a second time — which
/// HistoryIndex refuses as out-of-order, latching itself Unavailable and wedging both the
/// historical reads and the height. Both schedulers therefore call this immediately above the
/// line that advances their committed block number, with nothing in between.
///
/// Not a coroutine and not fallible in the ordinary sense: publishing is pure memory under each
/// index's own unique lock. If it does throw (allocation, or the ascending-order invariant),
/// HistoryIndex latches itself Unavailable, closes its publish window and rethrows — every later
/// query then refuses instead of answering from a half-applied index.
inline void publishBlockHistory(MPTHistory& history, HistoryCommitStage&& stage)
{
    if (stage.state.staged)
    {
        history.state().publish(std::move(*stage.state.staged), std::move(stage.state.retired),
            stage.state.boundaryWritten);
    }
    if (stage.trie.staged)
    {
        history.trie().publish(std::move(*stage.trie.staged), std::move(stage.trie.retired),
            stage.trie.boundaryWritten);
    }
    // A store with nothing staged (depth 0) still had its window opened by the guard, and
    // publish() is what would otherwise close it. Closing is idempotent, so this is also the
    // whole of the guard's work on the success path.
    history.state().closePublishWindow();
    history.trie().closePublishWindow();
}

}  // namespace bcos::ledger::mpt::history
