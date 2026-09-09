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
 * @file MPTPruner.h
 * @brief MPTPruner — the reference-counting, windowed-deletion CommitObserver with fully
 *        in-memory counts: rebuilt by a reachability walk over the recent state roots at
 *        every startup; only expired "/mpt/" node deletions land in the block's WriteBatch
 *        (spec §4.8, §5.6)
 */
#pragma once

#include "Account.h"
#include "CommitObserver.h"
#include "Constants.h"
#include "Errors.h"
#include "NodeDecoder.h"
#include <bcos-framework/ledger/Features.h>
#include <bcos-framework/ledger/FeaturesStorage.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/GenesisStateRoot.h>
#include <bcos-storage/KeyPrefixes.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace bcos::ledger::mpt
{
#define MPT_PRUNER_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("MPT_PRUNER")

/// Reference-counting MPT pruning (spec §4.8), one instance per chain over the committed-state
/// backend (production: GlobalStateStorage::latestBackend()).
///
/// All pruning state is IN MEMORY — no refcount rows, queue rows, watermarks or seed markers are
/// persisted anywhere:
///  - m_counts:  hash → {count, deadline?}. One reference CREATION (each emission of the node by
///    a trie build — MPTDeltaLayer::refCountDeltas, NOT the deduplicated newNodes map) is +1; one
///    obsoletion is −1, saturating at 0. A count dropping to 0 schedules the deletion at
///    blockNumber + pruneWindow; rising back above 0 before then revokes the schedule.
///  - m_pending: deadline → hashes, the delete queue. Confirmed deletions erase the Entry (a
///    later revival re-creates it via its +1).
///
/// Because nothing is persisted, every startup REBUILDS both tables by walking the committed
/// tries (init): the window guarantee keeps the roots of [head−N, head] complete on disk, so the
/// walk is always resolvable. A restart therefore self-heals any drift, and enabling pruning on
/// an existing chain needs no seeding or guard — the rebuild covers whatever history is on disk.
///
/// Per block, coPreparePruneRows runs inside the commit coroutine under the commit mutex, BEFORE
/// the block's storage layers merge. It never touches m_counts/m_pending directly: the block's
/// effects are computed on a scratch OVERLAY (m_staged*, seeded from the entries the block
/// touches), which onCommit applies to the base tables. The commit flow calls onCommit only
/// after the block's WriteBatch landed, so a failed merge leaves the counts untouched and the
/// retried block's prepare recomputes from the pre-block state, producing the identical batch —
/// prepare is idempotent under commit retry (a direct apply would double-count the deltas and
/// could schedule the deletion of a node a parallel trie still references):
///  1. the block's refCountDeltas are applied to the overlay (schedules armed/revoked as above,
///     staged against the base queue);
///  2. the queue is consumed up to the current block in full (steady state the matured amount
///     is ≈ one block's delta): each candidate is re-checked against its entry AS UPDATED
///     BY THIS BLOCK (a node this very block revived reads count > 0 in the overlay and is not
///     deleted), and confirmed deletions (count == 0 AND deadline == the queue entry's
///     deadline — anything else is a stale entry left by a revival or a re-arm) go into the
///     batch's `deletions` as the single "/mpt/" node-row key. The commit flow applies the
///     deletions to prewriteStorage, so node deletions land in ONE WriteBatch with the block
///     data: no crash window, no worker thread racing a concurrent commit (the F2 review fix).
///
/// Window guarantee: a node referenced by the state of block r can only be obsoleted at some
/// block o > r, so its deletion is consumed at o + N >= r + N + 1 — every state root in
/// [head − N, head] keeps its full node set on disk (N + 1 provable states).
///
/// Startup rebuild (init), given head = currentBlock and the first MPT block firstMptBlock
/// (scenario B/L2: genesis, block 0; scenario A: feature_mpt_state_root's activation block + 1 —
/// the activation block itself still commits a legacy XOR root), window start
/// S = max(firstMptBlock, head−N):
///  - Phase 1 (counts): walk the head stateRoot's account trie WITHOUT dedup — every hash
///    encounter counts (a node shared by K storage tries holds K live references, matching
///    refCountDeltas' per-emission semantics); at each account leaf Account::decode yields the
///    storageRoot, non-empty storage tries are walked the same way.
///  - Phase 2 (deadlines): walk the roots head−1 .. S NEWEST-FIRST with subtree dedup against
///    everything already seen (Phase 1 plus the newer roots of this phase): a newly seen node is
///    one the head state no longer references, last referenced by the NEWEST root that still
///    holds it, s — it was obsoleted at s+1, so deadline = s+1+N (still in the future:
///    s >= head−N). Newest-first makes the first attribution the correct one; the deadline
///    keeps the node alive until every root referencing it has left the window. Each candidate
///    root's ROW is probed first: a missing root row means the chain previously ran a SMALLER
///    window and that block was already pruned (root(b) leaves at b+1+N_old) — a widened N
///    recovers only roots still on disk, so the walk stops there and the effective window
///    starts at the first surviving root.
///  - Phase 3 (first-sweep of pre-existing garbage): scan the "/mpt/" table; a row in neither
///    the counts nor the queue is unreachable garbage (historical leak, or nodes written before
///    pruning was enabled). Driven by storage.mpt_prune_sweep_garbage: disabled (the default)
///    SKIPS the scan entirely — counting the garbage would itself cost the full-table scan the
///    option exists to avoid, so a disabled boot only logs the hint to enable the sweep;
///    enabled deletes the rows WHILE scanning, in SWEEP_DELETE_CHUNK batches — the full garbage
///    set is never materialized (the range iterator survives deleting already-passed keys:
///    MemoryStorage's ordered index invalidates only erased elements, RocksDBStorage2 pins a
///    snapshot).
///
/// A chain whose MPT is not yet active at boot (head < activation) skips the rebuild entirely:
/// the activation block's full first build (FlatToMPT) emits every node as that block's
/// newNodes — natural seeding through the ordinary counting path.
///
/// Known gap: when an account is deleted outright (tombstone path, MPTBuilder), only its storage
/// ROOT is obsoleted and counted down; the subtree below it is not cascade-walked, so those
/// nodes leak until the account-deletion path actually appears (today no protocol operation
/// deletes a pre-existing account — EIP-6780). Phase 3 collects such leaked subtrees at the NEXT
/// restart. Birth-side counting is naturally exact.
///
/// @tparam Backend a storage2 ReadWriteStorage over (executor_v1::StateKey → storage::Entry)
///         readable through StateKeyView as well (Features::readFromStorage), with ordered
///         RANGE_SEEK support — RocksDBStorage2 and an ORDERED MemoryStorage both qualify.
template <class Backend>
class MPTPruner : public CommitObserver
{
public:
    /// Garbage sweep batching: each chunk is one WriteBatch (idempotent — a crash mid-sweep
    /// leaves the rest on disk, re-detected at the next boot).
    static constexpr size_t SWEEP_DELETE_CHUNK = 10'000;

    /// The stateRoot of block @p n (nullopt when that block's header is unavailable). A pre-MPT
    /// block may return its legacy XOR root — init truncates the walk at firstMptBlock via the
    /// features, so the callable need not distinguish.
    using StateRootLookup =
        std::function<bcos::task::Task<std::optional<bcos::h256>>(bcos::protocol::BlockNumber)>;

    /// Called after each deleted chunk of the startup sweep: rows deleted so far, garbage rows
    /// found so far (the total is unknown until the scan finishes; the last call reports the
    /// final total in both).
    using GarbageProgress = std::function<void(uint64_t done, uint64_t total)>;

    /// @param backend         the committed-state backend; every read below hits it directly
    ///                        (no cache layer may sit between). Must outlive the pruner.
    /// @param pruneWindow     N: a node whose refcount hits 0 at block b becomes deletable once
    ///                        block b + N is committed.
    MPTPruner(Backend& backend, int64_t pruneWindow)
      : m_backend(std::addressof(backend)), m_pruneWindow(pruneWindow)
    {}

    /// Rebuild the in-memory counts and delete queue from the committed state (see the class
    /// comment for the three phases). Runs synchronously at boot, before the scheduler starts
    /// committing — no concurrency. No persistence, no startup guard: any chain state with the
    /// window's roots intact rebuilds correctly. @p sweepGarbage mirrors
    /// storage.mpt_prune_sweep_garbage: false skips the Phase-3 garbage scan entirely (only an
    /// informational hint is logged), true deletes the unreachable rows while scanning.
    /// @throws MPTInvariantViolation when a
    /// reachable node row is missing (the trie is the source of truth — fail loud, same
    /// convention as Trie.h) or the head header carries no root.
    bcos::task::Task<void> init(bcos::protocol::BlockNumber currentBlock,
        StateRootLookup stateRootAt, bool sweepGarbage, GarbageProgress progress = {})
    {
        m_watermark.store(currentBlock, std::memory_order_relaxed);

        bcos::ledger::Features features;
        co_await features.readFromStorage(*m_backend, currentBlock);
        using Flag = bcos::ledger::Features::Flag;

        std::optional<bcos::protocol::BlockNumber> firstMptBlock;
        if (features.get(Flag::feature_l2_ethereum_compat))
        {
            firstMptBlock = 0;  // scenario B/L2: the genesis stateRoot is already an MPT root
        }
        else if (features.get(Flag::feature_mpt_state_root))
        {
            auto const activation = features.activationBlockOf(Flag::feature_mpt_state_root);
            if (activation >= 0)
            {
                firstMptBlock = activation + 1;
            }
        }
        if (!firstMptBlock || currentBlock < *firstMptBlock)
        {
            // MPT not active yet (or no MPT block committed): nothing to rebuild — the first
            // MPT block's delta emits every node as newNodes, seeding the counts naturally.
            MPT_PRUNER_LOG(INFO)
                << "MPT pruning: MPT inactive at boot, counts start empty (the activation "
                   "block's first build seeds them)"
                << LOG_KV("head", currentBlock);
            co_return;
        }

        auto const headRoot = co_await stateRootAt(currentBlock);
        if (!headRoot)
        {
            BOOST_THROW_EXCEPTION(
                MPTInvariantViolation{} << bcos::errinfo_comment(
                    "MPT pruning rebuild: no stateRoot available for the head block " +
                    std::to_string(currentBlock)));
        }

        // Phase 1: count the head state's references.
        std::unordered_set<bcos::h256> seen;
        co_await countWalk(*headRoot, true, seen);

        // Phase 2: newest-first over [head−N, head): attribute each no-longer-live node to the
        // newest root still referencing it, deadline = s+1+N. The oldest in-window root head−N
        // MUST be walked too: its unique nodes carry the future deadline head+1 in steady
        // state — skipping them would let Phase 3 misclassify them as garbage.
        auto const windowStart = std::max<bcos::protocol::BlockNumber>(
            *firstMptBlock, currentBlock - m_pruneWindow);
        for (auto block = currentBlock - 1; block >= windowStart; --block)
        {
            auto const root = co_await stateRootAt(block);
            if (!root)
            {
                break;  // older headers unavailable (pruned block data) — nothing more to walk
            }
            // Probe the root ROW first: a chain that previously ran a SMALLER window has
            // already deleted this block's root (root(b) leaves at b+1+N_old), and deadlineWalk
            // would fail loud on the missing row. Widening N recovers only roots still on disk,
            // so stop the downward walk here — the effective window starts at block+1, the
            // first surviving root (the same shape as a shrunken window).
            if (*root != emptyRootHash() &&
                !co_await bcos::storage2::readOne(
                    *m_backend, bcos::ledger::mptNodeStateKey(*root)))
            {
                MPT_PRUNER_LOG(INFO)
                    << "MPT pruning: in-window state root already pruned by a previous smaller "
                       "window — stopping the rebuild walk at the first surviving root"
                    << LOG_KV("block", block) << LOG_KV("effectiveWindowStart", block + 1)
                    << LOG_KV("pruneWindow", m_pruneWindow);
                break;
            }
            co_await deadlineWalk(
                *root, true, static_cast<uint64_t>(block + 1 + m_pruneWindow), seen);
        }

        // Phase 3: unreachable-garbage sweep. With the sweep disabled (the config default) the
        // scan is SKIPPED outright — even counting the garbage would cost the full-table
        // "/mpt/" scan the option exists to avoid on large chains. The hint is logged
        // unconditionally: historical garbage (a leak, or nodes written before pruning was
        // enabled) may sit on disk undetected until the operator opts in and restarts.
        if (!sweepGarbage)
        {
            MPT_PRUNER_LOG(INFO)
                << "MPT pruning: \"/mpt/\" garbage sweep disabled — skipping the startup scan "
                   "(historical unreachable rows, if any, stay on disk; set "
                   "storage.mpt_prune_sweep_garbage=true and restart to reclaim the space)"
                << LOG_KV("head", currentBlock);
            MPT_PRUNER_LOG(INFO) << "MPT pruning: reference counts rebuilt from the state roots"
                                 << LOG_KV("head", currentBlock)
                                 << LOG_KV("firstMptBlock", *firstMptBlock)
                                 << LOG_KV("windowStart", windowStart)
                                 << LOG_KV("tracked", m_counts.size())
                                 << LOG_KV("scheduled", pendingCount())
                                 << LOG_KV("sweepGarbage", false);
            co_return;
        }
        // sweepGarbage=true: scan the "/mpt/" table for unreachable rows (never counted, never
        // queued) and delete WHILE scanning in SWEEP_DELETE_CHUNK batches, never materializing
        // the full garbage set: the range iterator survives deleting already-passed keys
        // (MemoryStorage's ordered index invalidates only erased elements; RocksDBStorage2's
        // RANGE_SEEK pins a snapshot). Garbage rows are by definition not in m_counts, so no
        // in-memory table needs a fix-up.
        uint64_t garbage = 0;
        uint64_t garbageDeleted = 0;
        std::vector<bcos::executor_v1::StateKey> chunk;
        auto iterator = co_await bcos::storage2::range(*m_backend, bcos::storage2::RANGE_SEEK,
            bcos::executor_v1::StateKey{bcos::storage2::kMPTTable, std::string_view{}});
        while (auto item = co_await iterator.next())
        {
            auto const& key = std::get<0>(*item);
            bcos::executor_v1::StateKeyView const keyView{key};
            if (keyView.m_table != bcos::storage2::kMPTTable)
            {
                break;
            }
            if (!std::get_if<storage::Entry>(std::addressof(std::get<1>(*item))))
            {
                continue;  // tombstone on a logical-deletion backend: not a live node row
            }
            if (keyView.m_key.size() != bcos::h256::SIZE)
            {
                MPT_PRUNER_LOG(WARNING)
                    << "MPT pruning: skipping malformed \"/mpt/\" row (key part "
                    << keyView.m_key.size() << " bytes, expected 32)";
                continue;
            }
            bcos::h256 const hash{
                reinterpret_cast<bcos::byte const*>(keyView.m_key.data()), bcos::h256::SIZE};
            if (m_counts.contains(hash))
            {
                continue;
            }
            ++garbage;
            chunk.push_back(bcos::ledger::mptNodeStateKey(hash));
            if (chunk.size() >= SWEEP_DELETE_CHUNK)
            {
                garbageDeleted += chunk.size();
                co_await bcos::storage2::removeSome(*m_backend, std::move(chunk));
                chunk.clear();
                if (progress)
                {
                    progress(garbageDeleted, garbage);
                }
            }
        }
        if (!chunk.empty())
        {
            garbageDeleted += chunk.size();
            co_await bcos::storage2::removeSome(*m_backend, std::move(chunk));
            if (progress)
            {
                progress(garbageDeleted, garbage);
            }
        }

        MPT_PRUNER_LOG(INFO) << "MPT pruning: reference counts rebuilt from the state roots"
                             << LOG_KV("head", currentBlock)
                             << LOG_KV("firstMptBlock", *firstMptBlock)
                             << LOG_KV("windowStart", windowStart)
                             << LOG_KV("tracked", m_counts.size())
                             << LOG_KV("scheduled", pendingCount())
                             << LOG_KV("garbage", garbage)
                             << LOG_KV("garbageDeleted", garbageDeleted)
                             << LOG_KV("sweepGarbage", sweepGarbage);
        m_lastSweepDeleted = garbageDeleted;
    }

    /// The pruning rows for @p blockNumber: only the deletions of expired nodes — pruning keeps
    /// no metadata on disk. Pure in-memory computation plus no reads; issues no writes itself.
    /// IDEMPOTENT under commit retry: the block's effects are staged on the m_staged* overlay
    /// and land on m_counts/m_pending only in onCommit (which the commit flow calls after the
    /// block's WriteBatch landed), so a retried block recomputes from the pre-block state and
    /// produces the identical batch — applying the refCountDeltas twice would schedule (and
    /// eventually delete) nodes a parallel trie still references.
    bcos::task::Task<PruneRowBatch> coPreparePruneRows(
        bcos::protocol::BlockNumber blockNumber, MPTDeltaLayer const& delta) override
    {
        PruneRowBatch out;
        // Fresh overlay per prepare; entries it does not touch read through to the base tables.
        // A previous uncommitted overlay (a failed commit whose retry this is) is DISCARDED —
        // the retry re-derives everything from the pre-block state.
        m_stagedCounts.clear();
        m_stagedDeadlineErases.clear();
        m_stagedDeadlineInserts.clear();

        // A delta that changed nodes but carries an EMPTY refCountDeltas means the tally was
        // never kept (a build run with trackRefCounts=false — production avoids this via
        // needsRefCountDeltas — so reaching here is a producer WIRING bug). The set reading
        // this would fall back to (+1 per newNodes hash, −1 per obsoleted/intraBlock hash)
        // cannot see duplicate emissions: content-addressed nodes shared across this block's
        // tries are created once per referencing trie but appear once in the deduplicated
        // newNodes map, so creations would be under-counted and a later obsoletion would
        // delete a node another trie still references (MPTDeltaLayer::refCountDeltas'
        // comment). Fail LOUD, not skip: skipping is not a pure leak either — nodes born in
        // the skipped block stay uncounted forever, so their later obsoletion could still
        // delete a live node. Throwing fails this block's commit (coPreparePruneRows is
        // co_awaited on the BaselineScheduler commit path), surfacing the bug on the first
        // block it affects. A fully empty delta is the normal empty block and passes through
        // silently.
        if (delta.refCountDeltas.empty() &&
            (!delta.newNodes.empty() || !delta.obsoletedNodes.empty() ||
                !delta.intraBlockObsoleted.empty()))
        {
            BOOST_THROW_EXCEPTION(MPTInvariantViolation{}
                                  << bcos::errinfo_comment(
                                         "MPT pruning: block " + std::to_string(blockNumber) +
                                         " carries node changes but no refCountDeltas tally "
                                         "(newNodes=" +
                                         std::to_string(delta.newNodes.size()) +
                                         ", obsoletedNodes=" +
                                         std::to_string(delta.obsoletedNodes.size()) +
                                         ", intraBlockObsoleted=" +
                                         std::to_string(delta.intraBlockObsoleted.size()) +
                                         ") — the producer ran without trackRefCounts; check "
                                         "the observer's needsRefCountDeltas wiring"));
        }

        auto const horizon = static_cast<uint64_t>(blockNumber);

        // Seed the overlay with a copy of every base entry the block can touch: the movements'
        // hashes, plus every hash sitting in an expired queue bucket (the consumption re-check
        // below reads those entries as updated by this block).
        for (auto const& [hash, movement] : delta.refCountDeltas)
        {
            if (auto const it = m_counts.find(hash); it != m_counts.end())
            {
                m_stagedCounts.emplace(hash, it->second);
            }
        }
        for (auto const& [deadline, bucket] : m_pending)
        {
            if (deadline > horizon)
            {
                break;
            }
            for (auto const& hash : bucket)
            {
                if (m_stagedCounts.contains(hash))
                {
                    continue;
                }
                if (auto const it = m_counts.find(hash); it != m_counts.end())
                {
                    m_stagedCounts.emplace(hash, it->second);
                }
            }
        }

        // Apply the block's reference movements ON THE OVERLAY. A node this block revived
        // (0→>0) reads as alive for the deletion re-check below, because the overlay counts are
        // updated first. stageSchedule()/revoke() mutate the overlay entry and stage the
        // matching queue-bucket change against the base queue; onCommit applies both.
        for (auto const& [hash, movement] : delta.refCountDeltas)
        {
            auto& entry = m_stagedCounts[hash];  // seeded above, or a fresh {0, none}
            uint64_t const newCount = static_cast<uint64_t>(
                std::max<int64_t>(0, static_cast<int64_t>(entry.count) + movement));
            bool const wasObsoleted = delta.obsoletedNodes.contains(hash) ||
                                      delta.intraBlockObsoleted.contains(hash);
            if (newCount == 0 && wasObsoleted && !entry.deadline)
            {
                // >0→0, or the saturating 0→0 of a node with no counted history: schedule the
                // deletion.
                stageSchedule(hash, static_cast<uint64_t>(blockNumber + m_pruneWindow));
            }
            else if (newCount > 0 && entry.deadline)
            {
                // 0→>0: revived before its deletion ran — revoke the schedule.
                revoke(hash, entry);
            }
            entry.count = newCount;
        }

        // Consume the expired delete queue, oldest deadline first, in full — in steady state
        // the matured amount is ≈ one block's delta. The CONSUMED view is the base queue minus
        // the staged erases (a bucket entry this block's movements already revoked is skipped);
        // the removals themselves are only staged — onCommit mutates the base buckets.
        for (auto bucketIt = m_pending.begin();
             bucketIt != m_pending.end() && bucketIt->first <= horizon; ++bucketIt)
        {
            uint64_t const deadline = bucketIt->first;
            for (auto const& hash : bucketIt->second)
            {
                if (auto const erasedIt = m_stagedDeadlineErases.find(deadline);
                    erasedIt != m_stagedDeadlineErases.end() && erasedIt->second.contains(hash))
                {
                    continue;  // revoked by this block's movements — never a candidate
                }
                // Re-check before deleting: a queue entry is only a hint — the count entry (AS
                // UPDATED BY THIS BLOCK, read overlay-then-base) is the verdict. count == 0 AND
                // deadline == this entry's deadline confirms the schedule was never revoked or
                // re-armed (a re-armed node carries a NEWER deadline, mismatching this stale
                // entry).
                Entry const* entry = nullptr;
                if (auto const stagedIt = m_stagedCounts.find(hash);
                    stagedIt != m_stagedCounts.end())
                {
                    entry = std::addressof(stagedIt->second);
                }
                else if (auto const baseIt = m_counts.find(hash); baseIt != m_counts.end())
                {
                    entry = std::addressof(baseIt->second);
                }
                bool const confirmed = entry != nullptr && entry->count == 0 &&
                                       entry->deadline == deadline &&
                                       // Unreachable under correct accounting (an emission this
                                       // block implies a positive post-block count), kept as a
                                       // belt-and-braces: never delete a node this block's own
                                       // flush is writing in the same WriteBatch — a leak
                                       // (retryable next block) beats a deleted live node.
                                       !delta.newNodes.contains(hash);
                if (confirmed)
                {
                    out.deletions.push_back(bcos::ledger::mptNodeStateKey(hash));
                    // onCommit erases the base entry (a later revival re-creates it via its +1).
                    auto& staged = m_stagedCounts[hash];
                    staged.count = 0;
                    staged.deadline.reset();
                }
                m_stagedDeadlineErases[deadline].insert(hash);  // consumed either way
            }
        }

        // Steady-state observability. `out.deletions` IS the batch handed back to the caller —
        // this block's CONFIRMED deletions. The staged counting (m_staged*) is not yet
        // committed, so tracked/pending/nextDeadline deliberately read the BASE tables: the
        // pre-block committed state, not the overlay (onCommit applies the staged changes only
        // after the block's WriteBatch landed).
        if (!out.deletions.empty())
        {
            MPT_PRUNER_LOG(DEBUG)
                << "MPT pruning: block deletions confirmed" << LOG_KV("block", blockNumber)
                << LOG_KV("deletions", out.deletions.size());
        }
        if (blockNumber % summaryLogInterval(m_pruneWindow) == 0)
        {
            auto const nextDeadline = nextPendingDeadline();
            MPT_PRUNER_LOG(INFO)
                << "MPT pruning: steady-state summary" << LOG_KV("block", blockNumber)
                << LOG_KV("deletions", out.deletions.size())
                << LOG_KV("tracked", trackedCount()) << LOG_KV("pending", pendingCount())
                << LOG_KV("nextDeadline",
                       nextDeadline ? std::to_string(*nextDeadline) : std::string{"-"});
        }
        co_return out;
    }

    /// The pruner counts references from the delta — the build must maintain the tally.
    bool needsRefCountDeltas() const noexcept override { return true; }

    /// After the block's WriteBatch: apply the staged overlay to the base tables and advance
    /// the in-memory watermark. coPreparePruneRows staged the block's counting work precisely so
    /// that a failed merge (which never reaches this hook) leaves the counts untouched and the
    /// commit retry reproduces the identical batch. Deletions already landed with the batch —
    /// there is nothing to hand off. The CommitObserver contract forbids throwing and blocking
    /// here.
    void onCommit(
        bcos::protocol::BlockNumber blockNumber, MPTDeltaLayer const& /*delta*/) override
    {
        for (auto const& [deadline, hashes] : m_stagedDeadlineErases)
        {
            if (auto const bucketIt = m_pending.find(deadline); bucketIt != m_pending.end())
            {
                for (auto const& hash : hashes)
                {
                    bucketIt->second.erase(hash);
                }
                if (bucketIt->second.empty())
                {
                    m_pending.erase(bucketIt);
                }
            }
        }
        for (auto const& [deadline, hashes] : m_stagedDeadlineInserts)
        {
            auto& bucket = m_pending[deadline];
            bucket.insert(hashes.begin(), hashes.end());
        }
        for (auto const& [hash, staged] : m_stagedCounts)
        {
            if (staged.count == 0 && !staged.deadline)
            {
                m_counts.erase(hash);  // confirmed-deleted, or never tracked
            }
            else
            {
                m_counts[hash] = staged;
            }
        }
        m_stagedCounts.clear();
        m_stagedDeadlineErases.clear();
        m_stagedDeadlineInserts.clear();

        auto current = m_watermark.load(std::memory_order_relaxed);
        while (current < blockNumber &&
               !m_watermark.compare_exchange_weak(
                   current, blockNumber, std::memory_order_relaxed))
        {
        }
    }

    /// Highest committed block number this pruner has seen (currentBlock at init(), onCommit
    /// afterwards). −1 before either. Purely observational — nothing is persisted.
    bcos::protocol::BlockNumber watermark() const noexcept
    {
        return m_watermark.load(std::memory_order_relaxed);
    }

    /// Outcome of the latest startup garbage sweep (init Phase 3), for logging/tooling.
    uint64_t lastSweepDeleted() const noexcept { return m_lastSweepDeleted; }

    /// The tracked reference count of @p hash, or nullopt when untracked (never seen, or
    /// already deleted and erased). Introspection for tests and tooling.
    std::optional<uint64_t> countOf(bcos::h256 const& hash) const
    {
        auto const it = m_counts.find(hash);
        if (it == m_counts.end())
        {
            return std::nullopt;
        }
        return it->second.count;
    }

    /// The deletion deadline currently armed for @p hash, or nullopt when none.
    std::optional<uint64_t> deadlineOf(bcos::h256 const& hash) const
    {
        auto const it = m_counts.find(hash);
        if (it == m_counts.end())
        {
            return std::nullopt;
        }
        return it->second.deadline;
    }

    /// Total scheduled deletions across all deadlines.
    size_t pendingCount() const noexcept
    {
        size_t total = 0;
        for (auto const& [deadline, bucket] : m_pending)
        {
            total += bucket.size();
        }
        return total;
    }

    /// The earliest armed deadline, or nullopt when the queue is empty.
    std::optional<uint64_t> nextPendingDeadline() const noexcept
    {
        if (m_pending.empty())
        {
            return std::nullopt;
        }
        return m_pending.begin()->first;
    }

    /// Number of hashes with a count entry (counted live nodes plus scheduled ones).
    size_t trackedCount() const noexcept { return m_counts.size(); }

    /// Steady-state observability: coPreparePruneRows emits one INFO summary every
    /// summaryLogInterval(m_pruneWindow) blocks. The interval tracks the prune window — the
    /// natural cadence of the pruning cycle — with a 100-block floor: a small N (a test chain,
    /// or an aggressive production config) would otherwise print one line per block. The
    /// trade-off is that with N < 100 the summary is sparser than one-per-window; the logged
    /// block number still makes the cadence unambiguous, and the per-batch DEBUG line keeps
    /// every actual deletion visible between summaries.
    static constexpr int64_t SUMMARY_LOG_MIN_INTERVAL = 100;
    static constexpr int64_t summaryLogInterval(int64_t pruneWindow)
    {
        return std::max<int64_t>(pruneWindow, SUMMARY_LOG_MIN_INTERVAL);
    }

private:
    struct Entry
    {
        uint64_t count{0};
        std::optional<uint64_t> deadline{};
    };

    /// The raw RLP of the hash-addressed node @p hash. A missing row violates the window
    /// guarantee the rebuild relies on — fail loud, same convention as Trie.h.
    bcos::task::Task<bcos::bytes> readNodeOrThrow(bcos::h256 const& hash) const
    {
        auto entry =
            co_await bcos::storage2::readOne(*m_backend, bcos::ledger::mptNodeStateKey(hash));
        if (!entry)
        {
            BOOST_THROW_EXCEPTION(MPTInvariantViolation{}
                                  << bcos::errinfo_comment(
                                         "MPT pruning rebuild: reachable node row missing from "
                                         "the committed backend (hash " +
                                         hash.abridged() + ")"));
        }
        auto raw = entry->get();
        co_return bcos::bytes(raw.begin(), raw.end());
    }

    /// The hash-addressed children of @p node, plus — for an account-trie leaf — the account's
    /// storage root. Inline node refs are embedded in their parent and never stored as rows, so
    /// they carry no count and are not descended into (an inline subtree is < 32 bytes and can
    /// hold no hash ref of its own).
    static void descend(TrieNode const& node, bool accountTrie,
        std::vector<std::pair<bcos::h256, bool>>& stack)
    {
        if (auto const* ext = std::get_if<ExtensionNode>(&node))
        {
            if (ext->child.size() == HASH_REF_ENCODED_SIZE && ext->child[0] == RLP_HASH_REF_PREFIX)
            {
                stack.emplace_back(
                    bcos::h256(bcos::bytesConstRef(ext->child.data(), ext->child.size())
                                   .getCroppedData(1)),
                    accountTrie);
            }
        }
        else if (auto const* branch = std::get_if<BranchNode>(&node))
        {
            for (auto const& child : branch->children)
            {
                if (child.kind() == NodeRef::Kind::Hash)
                {
                    stack.emplace_back(child.hash(), accountTrie);
                }
            }
        }
        else if (accountTrie)
        {
            if (auto const* leaf = std::get_if<LeafNode>(&node))
            {
                auto const account = Account::decode(bcos::ref(leaf->value));
                if (account.storageRoot != emptyRootHash())
                {
                    stack.emplace_back(account.storageRoot, false);
                }
            }
        }
    }

    /// Phase 1: count EVERY encounter of each hash-addressed node reachable from @p root (no
    /// dedup — K referencing tries are K live references), recording the encountered set into
    /// @p seen for Phase 2's attribution.
    bcos::task::Task<void> countWalk(
        bcos::h256 root, bool accountTrie, std::unordered_set<bcos::h256>& seen)
    {
        if (root == emptyRootHash())
        {
            co_return;
        }
        std::vector<std::pair<bcos::h256, bool>> stack{{root, accountTrie}};
        while (!stack.empty())
        {
            auto const [hash, isAccount] = stack.back();
            stack.pop_back();
            seen.insert(hash);
            ++m_counts[hash].count;
            auto const raw = co_await readNodeOrThrow(hash);
            descend(decodeNode(bcos::ref(raw)), isAccount, stack);
        }
    }

    /// Phase 2: walk @p root's trie, skipping any subtree root already in @p seen (owned by the
    /// head state or a newer root); newly seen nodes are no longer live and get
    /// @p deadline = s+1+N with s the walked root's block.
    bcos::task::Task<void> deadlineWalk(bcos::h256 root, bool accountTrie, uint64_t deadline,
        std::unordered_set<bcos::h256>& seen)
    {
        if (root == emptyRootHash())
        {
            co_return;
        }
        std::vector<std::pair<bcos::h256, bool>> stack{{root, accountTrie}};
        while (!stack.empty())
        {
            auto const [hash, isAccount] = stack.back();
            stack.pop_back();
            if (!seen.insert(hash).second)
            {
                continue;
            }
            schedule(hash, deadline);
            auto const raw = co_await readNodeOrThrow(hash);
            descend(decodeNode(bcos::ref(raw)), isAccount, stack);
        }
    }

    /// Arm @p hash's deletion at @p deadline on the BASE tables (its entry is created when
    /// absent — count 0). The init rebuild path only: the per-block path stages instead, so a
    /// failed commit leaves no trace (stageSchedule).
    void schedule(bcos::h256 const& hash, uint64_t deadline)
    {
        auto& entry = m_counts[hash];
        entry.deadline = deadline;
        m_pending[deadline].insert(hash);
    }

    /// Arm @p hash's deletion at @p deadline on the overlay: the deadline lands on the staged
    /// entry and the queue insert is staged for onCommit. The per-block prepare path only.
    void stageSchedule(bcos::h256 const& hash, uint64_t deadline)
    {
        m_stagedCounts[hash].deadline = deadline;
        m_stagedDeadlineInserts[deadline].insert(hash);
    }

    /// Revoke @p hash's pending deletion on the overlay: stage the removal from its base-queue
    /// bucket (a no-op there when the deadline was armed by this same prepare — but then the
    /// staged INSERT is dropped instead) and clear the staged entry's deadline.
    void revoke(bcos::h256 const& hash, Entry& entry)
    {
        if (entry.deadline)
        {
            m_stagedDeadlineErases[*entry.deadline].insert(hash);
            if (auto const insIt = m_stagedDeadlineInserts.find(*entry.deadline);
                insIt != m_stagedDeadlineInserts.end())
            {
                insIt->second.erase(hash);
                if (insIt->second.empty())
                {
                    m_stagedDeadlineInserts.erase(insIt);
                }
            }
        }
        entry.deadline.reset();
    }

    Backend* m_backend;
    int64_t m_pruneWindow;
    std::atomic<int64_t> m_watermark{-1};
    uint64_t m_lastSweepDeleted = 0;
    std::unordered_map<bcos::h256, Entry> m_counts;
    std::map<uint64_t, std::unordered_set<bcos::h256>> m_pending;

    /// The per-block scratch overlay (see the class comment): populated by coPreparePruneRows,
    /// applied to m_counts/m_pending by onCommit, and empty outside a commit — the commit flow
    /// serializes prepare → merge → onCommit under the commit mutex, and a retried prepare
    /// discards and recomputes the overlay from the pre-block state. m_stagedCounts overlays
    /// the count entries the block touches (read overlay-then-base); a staged {count 0, no
    /// deadline} entry tells onCommit to ERASE the base entry (a confirmed deletion — or a
    /// never-tracked hash, whose erase is a no-op). m_stagedDeadlineErases/Inserts are the
    /// queue-bucket changes to replay against m_pending.
    std::unordered_map<bcos::h256, Entry> m_stagedCounts;
    std::map<uint64_t, std::unordered_set<bcos::h256>> m_stagedDeadlineErases;
    std::map<uint64_t, std::unordered_set<bcos::h256>> m_stagedDeadlineInserts;
};

}  // namespace bcos::ledger::mpt
