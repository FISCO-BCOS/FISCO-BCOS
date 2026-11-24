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
 * @brief MPTPruner — reference-counting, windowed deletion with counts inline in the
 *        node storage RLP, a persistent deadline queue, and on-demand batched reads;
 *        node updates, deletions and queue changes share the block's WriteBatch
 *        (spec §4.8, §5.6)
 */
#pragma once

#include "Account.h"
#include "CommitObserver.h"
#include "Constants.h"
#include "Errors.h"
#include "NodeDecoder.h"
#include "NodeStorageCodec.h"
#include <bcos-framework/ledger/Features.h>
#include <bcos-framework/ledger/FeaturesStorage.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/GenesisStateRoot.h>
#include <bcos-storage/KeyPrefixes.h>
#include <bcos-task/Immediate.h>
#include <bcos-task/TBBWait.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <bcos-utilities/LogStream.h>
#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/task_group.h>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace bcos::ledger::mpt
{
#define MPT_PRUNER_LOG(LEVEL) BCOS_MODULE_LOG(LEDGER, LEVEL) << LOG_BADGE("MPT_PRUNER")

/// Whether the Phase-3 garbage sweep may scan the "/mpt/" table with CONCURRENT shard
/// iterators: the backend must tolerate independent RANGE_SEEK iterators plus delete batches
/// issued from multiple threads at once. Default FALSE — a backend opts IN explicitly by
/// declaring `static constexpr bool kConcurrentSweepSafe = true`, so a new backend or wrapper
/// author has to answer the question instead of silently getting concurrent deletes.
/// RocksDBStorage2 opts in at its definition (each RANGE_SEEK pins its own snapshot, writes
/// go through the thread-safe DB handle). The in-memory backends do not: a non-CONCURRENT
/// MemoryStorage is a single unsynchronized index, and a CONCURRENT one shards its ordered
/// index per bucket, losing the global key ordering the shard spans rely on — every backend
/// without the marker takes the single-shard sequential path.
///
/// Concurrent READS are a separate, weaker requirement and are assumed safe on every
/// backend: walkTrie issues up to WALK_READ_WAYS concurrent readSome/MultiGet calls during
/// the rebuild phases, which issue no concurrent WRITES — read/read concurrency needs no
/// synchronization even on a single unsynchronized index. Only the sweep mixes concurrent
/// reads with delete batches, hence this trait.
template <class Backend, class = void>
inline constexpr bool kParallelSweepSafe = false;
template <class Backend>
inline constexpr bool
    kParallelSweepSafe<Backend, std::void_t<decltype(Backend::kConcurrentSweepSafe)>> =
        Backend::kConcurrentSweepSafe;

/// Whether an awaitable type is a bcos::task::Immediate — the only awaitable that
/// NEVER suspends by construction (await_ready is always true).
template <class T>
inline constexpr bool kIsImmediate = false;
template <class V>
inline constexpr bool kIsImmediate<bcos::task::Immediate<V>> = true;

/// Compile-time enforcement of the sweep's parallel_for safety argument (see init Phase 3):
/// when the sweep may run multi-shard, the BACKEND-level storage calls must be
/// never-suspending Immediates, so tbb::syncWait always takes its ready path and never
/// blocks a TBB worker under parallel_for. A backend that goes truly asynchronous breaks the
/// build here instead of hanging the boot. Backends without the opt-in marker run the
/// single-shard path and have nothing to prove — the expressions are not even formed.
template <class Backend, bool = kParallelSweepSafe<Backend>>
struct SweepInlineCompletionCheck
{
    static constexpr bool value = true;
};
template <class Backend>
struct SweepInlineCompletionCheck<Backend, true>
{
    using RangeAwaitable = decltype(std::declval<Backend&>().range(
        bcos::storage2::RANGE_SEEK, std::declval<bcos::executor_v1::StateKey&>()));
    using Iterator = bcos::task::AwaitableReturnType<RangeAwaitable>;
    static constexpr bool value = kIsImmediate<RangeAwaitable> &&
                                  kIsImmediate<decltype(std::declval<Iterator&>().next())> &&
                                  kIsImmediate<decltype(std::declval<Backend&>().removeSome(
                                      std::declval<std::vector<bcos::executor_v1::StateKey>>()))>;
};

/// Reference-counting MPT pruning (spec §4.8), one instance per chain over the committed-state
/// backend (production: GlobalStateStorage::latestBackend()).
///
/// Persistent pruning state:
///  - each "/mpt/" row stores [refCount, deadline, rawNodeRlp]. The hash is still computed only
///    over rawNodeRlp; the metadata never changes the state root.
///  - the deadline queue is persisted separately and cached as m_pending. Node and queue updates
///    share the block's WriteBatch. Head root, tracked count and prune window are also persisted.
///  - m_counts is used only as a temporary reachability map during an explicit rebuild. Normal
///    startup restores metadata and the queue, then reads counts on demand — it does not populate
///    a full in-memory count map. A missing/mismatched head, changed window or rollback forces a
///    reachability rebuild and writes its result back to the node rows.
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
/// Explicit rebuild (forceRebuild, missing/mismatched metadata, or a garbage sweep), given head
/// = currentBlock and the first MPT block firstMptBlock
/// (scenario B/L2: genesis, block 0; scenario A: feature_mpt_state_root's activation block + 1 —
/// the activation block itself still commits a legacy XOR root), window start
/// S = max(firstMptBlock, head−N):
///  - Phase 1 (counts): walk the head stateRoot's account trie WITHOUT dedup — every hash
///    encounter counts (a node shared by K storage tries holds K live references, matching
///    refCountDeltas' per-emission semantics); at each account leaf Account::decode yields the
///    storageRoot, non-empty storage tries are walked the same way.
///  - Phase 2 (deadlines): walk the roots head−1 .. S NEWEST-FIRST with subtree dedup against
///    everything already counted (m_counts IS the seen set: Phase 1 plus the newer roots of
///    this phase): a newly seen node is
///    one the head state no longer references, last referenced by the NEWEST root that still
///    holds it, s — it was obsoleted at s+1, so deadline = s+1+N (still in the future:
///    s >= head−N). Newest-first makes the first attribution the correct one; the deadline
///    keeps the node alive until every root referencing it has left the window. Each candidate
///    root's ROW is probed first: a missing root row means the chain previously ran a SMALLER
///    window and that block was already pruned (root(b) leaves at b+1+N_old) — a widened N
///    recovers only roots still on disk, so the walk stops there and the effective window
///    starts at the first surviving root.
///    Phases 1 and 2 share ONE traversal implementation (walkTrie): the stack, the batched
///    node-row read (storage2::readSome, MultiGet on RocksDB) and the
///    descend rules live there once; each phase supplies only its per-node callback
///    (count-and-descend vs. dedup-against-m_counts-then-arm).
///  - Phase 3 (first-sweep of pre-existing garbage): scan the "/mpt/" table; a row in neither
///    the counts nor the queue is unreachable garbage (historical leak, or nodes written before
///    pruning was enabled). Driven by storage.mpt_prune_sweep_garbage: disabled (the default)
///    SKIPS the scan entirely — counting the garbage would itself cost the full-table scan the
///    option exists to avoid, so a disabled boot only logs the hint to enable the sweep;
///    enabled deletes the rows WHILE scanning, in SWEEP_DELETE_CHUNK batches — the full garbage
///    set is never materialized (each range iterator survives deleting already-passed keys:
///    MemoryStorage's ordered index invalidates only erased elements, RocksDBStorage2 pins a
///    snapshot). The scan runs sharded: one tbb::parallel_for body per first hash byte
///    (SWEEP_SHARDS spans); backends that disallow concurrent iterators (kParallelSweepSafe —
///    the in-memory test backends) take the same code with a single table-wide shard.
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

    /// Garbage sweep sharding: the "/mpt/" row key is (table prefix + 32 raw hash bytes), so
    /// one first-byte span per shard partitions the table into 256 disjoint key ranges, each
    /// scanned by its own tbb task. A single sequential iterator leaves every core but one
    /// idle on the scan loop (the per-row cost is tiny, so the iterator advance itself is the
    /// bottleneck); 256 shards let the arena keep ~hardware_concurrency spans in flight, and a
    /// dense span cannot strand the rest of the table behind one thread.
    static constexpr size_t SWEEP_SHARDS = 256;

    /// Startup progress logging cadence. Phase 1 walks the entire head trie and the phase-3
    /// sweep scans the entire "/mpt/" table — on an archive-scale database both run for a
    /// long time, and without a periodic line the boot looks hung. Phase 1 logs one line per
    /// REBUILD_LOG_INTERVAL visited nodes, the sweep one per SWEEP_SCAN_LOG_INTERVAL examined
    /// rows (phase 2 logs per block, so it needs no counter).
    static constexpr uint64_t REBUILD_LOG_INTERVAL = 1'000'000;
    static constexpr uint64_t SWEEP_SCAN_LOG_INTERVAL = 10'000'000;

    /// The stateRoot of block @p n (nullopt when that block's header is unavailable). A pre-MPT
    /// block may return its legacy XOR root — init truncates the walk at firstMptBlock via the
    /// features, so the callable need not distinguish. Aliased from the observer base so the
    /// rollback hook (coOnRollback) and the startup rebuild share one type.
    using StateRootLookup = CommitObserver::StateRootLookup;

    /// Called after each deleted chunk of the startup sweep: rows deleted so far, garbage rows
    /// found so far (the total is unknown until the scan finishes; the last call reports the
    /// final total in both). On a parallel-sweep-safe backend the shards make progress
    /// concurrently, but the pruner SERIALIZES every invocation on an internal mutex, so the
    /// callback keeps its original single-threaded contract; the arguments are a monotone
    /// snapshot (a later call never reports smaller numbers).
    using GarbageProgress = std::function<void(uint64_t done, uint64_t total)>;

    /// @param backend         the committed-state backend; every read below hits it directly
    ///                        (no cache layer may sit between). Must outlive the pruner.
    /// @param pruneWindow     N: a node whose refcount hits 0 at block b becomes deletable once
    ///                        block b + N is committed.
    MPTPruner(Backend& backend, int64_t pruneWindow)
      : m_backend(std::addressof(backend)), m_pruneWindow(pruneWindow)
    {}

    /// Restore persistent pruning state at boot. A matching head and prune window loads only
    /// metadata and the deadline queue; otherwise the three rebuild phases run synchronously
    /// before the scheduler starts committing. @p forceRebuild forces that reachability walk;
    /// @p allowRebuild=false refuses it with an actionable error instead of allocating the map.
    /// @p sweepGarbage mirrors
    /// storage.mpt_prune_sweep_garbage: false skips the Phase-3 garbage scan entirely (only an
    /// informational hint is logged), true deletes the unreachable rows while scanning.
    /// @throws MPTInvariantViolation when a
    /// reachable node row is missing (the trie is the source of truth — fail loud, same
    /// convention as Trie.h) or the head header carries no root.
    bcos::task::Task<void> init(bcos::protocol::BlockNumber currentBlock, int64_t executorVersion,
        StateRootLookup stateRootAt, bool sweepGarbage, GarbageProgress progress = {},
        bool forceRebuild = false, bool allowRebuild = true)
    {
        m_watermark.store(currentBlock, std::memory_order_relaxed);
        m_executorVersion = executorVersion;
        m_allowRebuild = allowRebuild;

        bcos::ledger::Features features;
        co_await features.readFromStorage(*m_backend, currentBlock);
        using Flag = bcos::ledger::Features::Flag;

        std::optional<bcos::protocol::BlockNumber> firstMptBlock;
        if (executorVersion >= bcos::ledger::ETHEREUM_EXECUTOR_VERSION)
        {
            firstMptBlock = 0;  // Ethereum lane: the genesis stateRoot is already an MPT root
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

        m_headRoot = *headRoot;
        auto const metadata = co_await readMetadataState(*headRoot);
        if ((forceRebuild || sweepGarbage || !metadata.present || !metadata.matches) &&
            !allowRebuild)
        {
            BOOST_THROW_EXCEPTION(
                MPTInvariantViolation{} << bcos::errinfo_comment(
                    "MPT pruning: reference-count rebuild requires a full reachability walk "
                    "and a large temporary in-memory map. Set "
                    "storage.mpt_prune_allow_rebuild=true and restart to allow it, or set "
                    "storage.mpt_prune_window=-1 to disable pruning"));
        }
        m_counts.clear();
        m_pending.clear();
        if (!forceRebuild && !sweepGarbage && metadata.present && metadata.matches)
        {
            m_trackedCount = metadata.tracked;
            co_await loadQueue();
            MPT_PRUNER_LOG(INFO) << "MPT pruning: restored persistent reference counts"
                                 << LOG_KV("head", currentBlock)
                                 << LOG_KV("tracked", m_trackedCount)
                                 << LOG_KV("scheduled", pendingCount());
            co_return;
        }

        m_trackedCount = 0;
        // Phase 1: count the head state's references.
        MPT_PRUNER_LOG(INFO) << "MPT pruning: rebuild phase 1 — counting the head state's "
                                "references (a full trie walk, progress logged every "
                             << REBUILD_LOG_INTERVAL << " nodes)" << LOG_KV("head", currentBlock);
        co_await countWalk(*headRoot, true);

        // Phase 2: newest-first over [head−N, head): attribute each no-longer-live node to the
        // newest root still referencing it, deadline = s+1+N. The oldest in-window root head−N
        // MUST be walked too: its unique nodes carry the future deadline head+1 in steady
        // state — skipping them would let Phase 3 misclassify them as garbage.
        auto const windowStart =
            std::max<bcos::protocol::BlockNumber>(*firstMptBlock, currentBlock - m_pruneWindow);
        for (auto block = currentBlock - 1; block >= windowStart; --block)
        {
            auto const root = co_await stateRootAt(block);
            if (!root)
            {
                // Older HEADERS unavailable (snapshot restore / pruned block data — NOT
                // pruning, which only ever touches "/mpt/" node rows): nothing more to walk.
                // Log loud like the pruned-root branch below — the effective window silently
                // collapses to the head otherwise.
                MPT_PRUNER_LOG(WARNING)
                    << "MPT pruning: block data unavailable (not pruned state) — stopping "
                       "the rebuild walk at the first block with a readable header"
                    << LOG_KV("block", block) << LOG_KV("effectiveWindowStart", block + 1);
                break;
            }
            // Probe the root ROW first: a chain that previously ran a SMALLER window has
            // already deleted this block's root (root(b) leaves at b+1+N_old), and deadlineWalk
            // would fail loud on the missing row. Widening N recovers only roots still on disk,
            // so stop the downward walk here — the effective window starts at block+1, the
            // first surviving root (the same shape as a shrunken window).
            if (*root != emptyRootHash() &&
                !co_await bcos::storage2::readOne(*m_backend, bcos::ledger::mptNodeStateKey(*root)))
            {
                MPT_PRUNER_LOG(INFO)
                    << "MPT pruning: in-window state root already pruned by a previous smaller "
                       "window — stopping the rebuild walk at the first surviving root"
                    << LOG_KV("block", block) << LOG_KV("effectiveWindowStart", block + 1)
                    << LOG_KV("pruneWindow", m_pruneWindow);
                break;
            }
            co_await deadlineWalk(*root, true, static_cast<uint64_t>(block + 1 + m_pruneWindow));
            MPT_PRUNER_LOG(INFO) << "MPT pruning: rebuild phase 2 — in-window root walked"
                                 << LOG_KV("block", block) << LOG_KV("windowStart", windowStart)
                                 << LOG_KV("tracked", m_counts.size())
                                 << LOG_KV("scheduled", pendingCount());
        }

        co_await persistRebuild(*headRoot);

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
            m_counts.clear();
            co_return;
        }
        // sweepGarbage=true: scan the "/mpt/" table for unreachable rows (never counted, never
        // queued) and delete WHILE scanning in SWEEP_DELETE_CHUNK batches, never materializing
        // the full garbage set: each range iterator survives deleting already-passed keys
        // (MemoryStorage's ordered index invalidates only erased elements; RocksDBStorage2's
        // RANGE_SEEK pins a snapshot). Garbage rows are by definition not in m_counts, so no
        // in-memory table needs a fix-up. The scan is SHARDED on the first hash byte:
        // tbb::parallel_for runs one body per span, driving the coroutine storage API with
        // tbb::syncWait. init runs before the scheduler starts committing, so m_counts is
        // READ-ONLY for the whole sweep and the shards share it without synchronization; the
        // counters are atomic. A backend without concurrent iterators (kParallelSweepSafe) runs
        // ONE shard spanning the whole table instead — the same code, sequential, with the
        // first-byte bound compiled out.
        //
        // parallel_for (not task_group) is safe HERE because every storage call below —
        // range(), iterator.next(), removeSome() — bottoms out in an Immediate that
        // completes synchronously inline, so tbb::syncWait always takes its ready path and
        // never reaches tbb::task::suspend; the TBBWait.h caveat (syncWait blocks the worker
        // under parallel_for once the awaited task really suspends) therefore never triggers.
        // SweepInlineCompletionCheck turns that into a build error the moment an opted-in
        // backend's calls go truly asynchronous — do NOT weaken the check instead. An
        // exception in any shard propagates out of parallel_for (siblings are cancelled) —
        // the fail-loud boot semantics this phase wants.
        static_assert(SweepInlineCompletionCheck<Backend>::value,
            "MPTPruner parallel sweep: the backend opted into kParallelSweepSafe but its "
            "range()/iterator.next()/removeSome() are not all never-suspending Immediates "
            "— tbb::syncWait could block a worker under parallel_for");
        constexpr size_t shardCount = kParallelSweepSafe<Backend> ? SWEEP_SHARDS : 1;
        std::atomic<uint64_t> scanned{0};
        std::atomic<uint64_t> garbage{0};
        std::atomic<uint64_t> garbageDeleted{0};
        // Serializes GarbageProgress invocations across shards — the callback contract
        // predates the sharded sweep and does not require thread-safety.
        std::mutex progressMutex;
        MPT_PRUNER_LOG(INFO) << "MPT pruning: garbage sweep — scanning the \"/mpt/\" table in "
                             << shardCount << " shard(s) (progress logged every "
                             << SWEEP_SCAN_LOG_INTERVAL << " rows)";
        oneapi::tbb::parallel_for(size_t{0}, shardCount,
            [this, &scanned, &garbage, &garbageDeleted, &progress, &progressMutex](size_t shard) {
                auto const startKey = [shard] {
                    (void)shard;  // unused when shardCount == 1 (clang -Wunused-lambda-capture)
                    if constexpr (shardCount > 1)
                    {
                        if (shard == 0)
                        {
                            // Shard 0 starts at the table's first key so it also visits
                            // malformed rows with an empty/short key part (they sort before
                            // every 32-byte key), keeping the warn-and-skip below reachable.
                            return bcos::executor_v1::StateKey{
                                bcos::storage2::kMPTTable, std::string_view{}};
                        }
                        // The shard's span: row keys whose first hash byte is `shard`.
                        bcos::h256 startHash{};
                        startHash.data()[0] = static_cast<bcos::byte>(shard);
                        return bcos::ledger::mptNodeStateKey(startHash);
                    }
                    else
                    {
                        return bcos::executor_v1::StateKey{
                            bcos::storage2::kMPTTable, std::string_view{}};
                    }
                }();
                auto iterator = bcos::task::tbb::syncWait(
                    bcos::storage2::range(*m_backend, bcos::storage2::RANGE_SEEK, startKey));
                std::vector<bcos::executor_v1::StateKey> chunk;
                while (auto item = bcos::task::tbb::syncWait(iterator.next()))
                {
                    auto const& key = std::get<0>(*item);
                    bcos::executor_v1::StateKeyView const keyView{key};
                    if (keyView.m_table != bcos::storage2::kMPTTable)
                    {
                        break;
                    }
                    if constexpr (shardCount > 1)
                    {
                        if (!keyView.m_key.empty() &&
                            static_cast<uint8_t>(keyView.m_key[0]) != shard)
                        {
                            break;  // past this shard's span
                        }
                    }
                    if ((scanned.fetch_add(1, std::memory_order_relaxed) + 1) %
                            SWEEP_SCAN_LOG_INTERVAL ==
                        0)
                    {
                        MPT_PRUNER_LOG(INFO)
                            << "MPT pruning: garbage sweep scan progress"
                            << LOG_KV("scanned", scanned.load(std::memory_order_relaxed))
                            << LOG_KV("garbage", garbage.load(std::memory_order_relaxed))
                            << LOG_KV(
                                   "garbageDeleted", garbageDeleted.load(std::memory_order_relaxed))
                            << LOG_KV("tracked", m_counts.size());
                    }
                    if (!std::get_if<storage::Entry>(std::addressof(std::get<1>(*item))))
                    {
                        continue;  // tombstone on a logical-deletion backend
                    }
                    if (keyView.m_key.size() != bcos::h256::SIZE)
                    {
                        MPT_PRUNER_LOG(WARNING)
                            << "MPT pruning: skipping malformed \"/mpt/\" row (key part "
                            << keyView.m_key.size() << " bytes, expected 32)";
                        continue;
                    }
                    bcos::h256 const hash{reinterpret_cast<bcos::byte const*>(keyView.m_key.data()),
                        bcos::h256::SIZE};
                    if (m_counts.contains(hash))
                    {
                        continue;
                    }
                    garbage.fetch_add(1, std::memory_order_relaxed);
                    chunk.push_back(bcos::ledger::mptNodeStateKey(hash));
                    if (chunk.size() >= SWEEP_DELETE_CHUNK)
                    {
                        // Release/acquire pairs on garbageDeleted keep the progress
                        // snapshot's done <= total: a shard that acquire-loads a deleted
                        // count also sees the garbage increments sequenced before it.
                        garbageDeleted.fetch_add(chunk.size(), std::memory_order_release);
                        bcos::task::tbb::syncWait(
                            bcos::storage2::removeSome(*m_backend, std::move(chunk)));
                        chunk.clear();
                        if (progress)
                        {
                            std::lock_guard const lock{progressMutex};
                            progress(garbageDeleted.load(std::memory_order_acquire),
                                garbage.load(std::memory_order_relaxed));
                        }
                    }
                }
                if (!chunk.empty())
                {
                    garbageDeleted.fetch_add(chunk.size(), std::memory_order_release);
                    bcos::task::tbb::syncWait(
                        bcos::storage2::removeSome(*m_backend, std::move(chunk)));
                    if (progress)
                    {
                        std::lock_guard const lock{progressMutex};
                        progress(garbageDeleted.load(std::memory_order_acquire),
                            garbage.load(std::memory_order_relaxed));
                    }
                }
            });

        MPT_PRUNER_LOG(INFO) << "MPT pruning: reference counts rebuilt from the state roots"
                             << LOG_KV("head", currentBlock)
                             << LOG_KV("firstMptBlock", *firstMptBlock)
                             << LOG_KV("windowStart", windowStart)
                             << LOG_KV("tracked", m_counts.size())
                             << LOG_KV("scheduled", pendingCount())
                             << LOG_KV("scanned", scanned.load(std::memory_order_relaxed))
                             << LOG_KV("garbage", garbage.load(std::memory_order_relaxed))
                             << LOG_KV("garbageDeleted",
                                    garbageDeleted.load(std::memory_order_relaxed))
                             << LOG_KV("sweepGarbage", sweepGarbage);
        m_lastSweepDeleted = garbageDeleted.load(std::memory_order_relaxed);
        m_counts.clear();
    }

    /// The pruning rows for @p blockNumber: node/metadata upserts, expired deletions and queue
    /// changes, computed from batched reads of only the affected nodes.
    /// IDEMPOTENT under commit retry: the block's effects are staged on the m_staged* overlay
    /// and written by the caller. onCommit updates only the in-memory queue and watermark after
    /// the block's WriteBatch landed, so a retried prepare recomputes from the pre-block state and
    /// produces the identical batch — applying the refCountDeltas twice would schedule (and
    /// eventually delete) nodes a parallel trie still references.
    bcos::task::Task<PruneRowBatch> coPreparePruneRows(
        bcos::protocol::BlockNumber blockNumber, MPTDeltaLayer const& delta) override
    {
        PruneRowBatch out;
        m_originalCounts.clear();
        m_stagedRaw.clear();
        m_stagedCounts.clear();
        m_stagedDeadlineErases.clear();
        m_stagedDeadlineInserts.clear();

        if (delta.refCountDeltas.empty() &&
            (!delta.newNodes.empty() || !delta.obsoletedNodes.empty() ||
                !delta.intraBlockObsoleted.empty()))
        {
            BOOST_THROW_EXCEPTION(
                MPTInvariantViolation{} << bcos::errinfo_comment(
                    "MPT pruning: block " + std::to_string(blockNumber) +
                    " carries node changes but no refCountDeltas tally "
                    "(newNodes=" +
                    std::to_string(delta.newNodes.size()) +
                    ", obsoletedNodes=" + std::to_string(delta.obsoletedNodes.size()) +
                    ", intraBlockObsoleted=" + std::to_string(delta.intraBlockObsoleted.size()) +
                    ") — the producer ran without trackRefCounts; check "
                    "the observer's needsRefCountDeltas wiring"));
        }

        auto const horizon = static_cast<uint64_t>(blockNumber);
        std::unordered_set<bcos::h256, H256FastHash> required;
        for (auto const& [hash, movement] : delta.refCountDeltas)
        {
            required.insert(hash);
        }
        for (auto const& [hash, raw] : delta.newNodes)
        {
            required.insert(hash);
            m_stagedRaw[hash] = raw;
        }
        for (auto const& [deadline, bucket] : m_pending)
        {
            if (deadline > horizon)
            {
                break;
            }
            required.insert(bucket.begin(), bucket.end());
        }
        for (auto const& hash : required)
        {
            m_stagedCounts[hash] = {};
        }
        auto storedNodes = co_await readStoredNodes(
            required | std::views::transform([](auto const& hash) { return hash; }) |
            std::ranges::to<std::vector>());
        for (auto& [hash, stored] : storedNodes)
        {
            Entry entry{.count = stored.refCount, .deadline = {}};
            if (stored.deadline != 0)
            {
                entry.deadline = stored.deadline;
            }
            m_originalCounts.emplace(hash, entry);
            m_stagedCounts[hash] = entry;
            m_stagedRaw[hash] = std::move(stored.rlp);
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
            bool const wasObsoleted =
                delta.obsoletedNodes.contains(hash) || delta.intraBlockObsoleted.contains(hash);
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
                Entry const* entry = std::addressof(m_stagedCounts[hash]);
                bool const confirmed = entry != nullptr && entry->count == 0 &&
                                       entry->deadline == deadline &&
                                       // Unreachable under correct accounting (an emission this
                                       // block implies a positive post-block count), kept as a
                                       // belt-and-braces: never delete a node this block's own
                                       // flush is writing in the same WriteBatch — a leak
                                       // beats a deleted live node.
                                       !delta.newNodes.contains(hash);
                if (confirmed)
                {
                    out.deletions.push_back(bcos::ledger::mptNodeStateKey(hash));
                    // onCommit erases the base entry (a later revival re-creates it via its +1).
                    auto& staged = m_stagedCounts[hash];
                    staged.count = 0;
                    staged.deadline.reset();
                    m_stagedDeadlineErases[deadline].insert(hash);
                }
                else if (entry == nullptr || entry->count != 0 || entry->deadline != deadline)
                {
                    // Disqualified for a self-healing reason — revoked, re-armed (a newer
                    // deadline mismatches this stale queue entry) or untracked: the hint is
                    // dead weight, consumed with the bucket.
                    m_stagedDeadlineErases[deadline].insert(hash);
                }
                else
                {
                    // count == 0 and the deadline matches, blocked ONLY by the newNodes
                    // belt-and-braces — unreachable under correct accounting, so worth a loud
                    // log. KEEP the queue entry: the next block's prepare re-checks it (the
                    // entry is consistent, just deferred), while erasing it here would orphan
                    // the count entry ({count 0, expired deadline}, referenced by no bucket)
                    // until a restart.
                    MPT_PRUNER_LOG(WARNING)
                        << "MPT pruning: the newNodes guard blocked a matured deletion whose "
                           "count entry still matches its schedule (unreachable under correct "
                           "accounting) — the queue entry is kept for re-check at the next "
                           "block"
                        << LOG_KV("block", blockNumber) << LOG_KV("deadline", deadline)
                        << LOG_KV("hash", hash.abridged());
                }
            }
        }

        for (auto const& [deadline, hashes] : m_stagedDeadlineErases)
        {
            for (auto const& hash : hashes)
            {
                out.queueDeletions.push_back(queueStateKey(deadline, hash));
            }
        }
        for (auto const& [deadline, hashes] : m_stagedDeadlineInserts)
        {
            for (auto const& hash : hashes)
            {
                out.queueUpserts.emplace_back(
                    queueStateKey(deadline, hash), makeEntry(std::string_view{}));
            }
        }

        int64_t tracked = static_cast<int64_t>(m_trackedCount);
        for (auto const& [hash, staged] : m_stagedCounts)
        {
            bool const before = m_originalCounts.contains(hash);
            bool const after = staged.count != 0 || staged.deadline.has_value();
            if (before != after)
            {
                tracked += after ? 1 : -1;
            }
            if (!after)
            {
                continue;
            }
            auto const original = m_originalCounts.find(hash);
            bool const changed = original == m_originalCounts.end() ||
                                 original->second.count != staged.count ||
                                 original->second.deadline != staged.deadline;
            if (!changed && !delta.newNodes.contains(hash))
            {
                continue;
            }
            auto const raw = m_stagedRaw.find(hash);
            if (raw == m_stagedRaw.end())
            {
                continue;
            }
            out.upserts.emplace_back(bcos::ledger::mptNodeStateKey(hash),
                makeEntry(encodeStoredNode(
                    bcos::ref(raw->second), staged.count, staged.deadline.value_or(0))));
        }
        m_stagedTrackedCount = static_cast<uint64_t>(std::max<int64_t>(tracked, 0));
        appendMetadata(out, m_stagedTrackedCount,
            delta.stateRoot == bcos::h256{} ? m_headRoot : delta.stateRoot);

        // Steady-state observability. `out.deletions` IS the batch handed back to the caller —
        // this block's CONFIRMED deletions. The staged counting (m_staged*) is not yet
        // committed, so tracked/pending/nextDeadline deliberately read the BASE tables: the
        // pre-block committed state, not the overlay (onCommit applies the staged changes only
        // after the block's WriteBatch landed).
        if (!out.deletions.empty())
        {
            MPT_PRUNER_LOG(DEBUG) << "MPT pruning: block deletions confirmed"
                                  << LOG_KV("block", blockNumber)
                                  << LOG_KV("deletions", out.deletions.size());
        }
        if (blockNumber % summaryLogInterval(m_pruneWindow) == 0)
        {
            auto const nextDeadline = nextPendingDeadline();
            MPT_PRUNER_LOG(INFO) << "MPT pruning: steady-state summary"
                                 << LOG_KV("block", blockNumber)
                                 << LOG_KV("deletions", out.deletions.size())
                                 << LOG_KV("tracked", trackedCount())
                                 << LOG_KV("pending", pendingCount())
                                 << LOG_KV("nextDeadline", nextDeadline ?
                                                               std::to_string(*nextDeadline) :
                                                               std::string{"-"});
        }
        co_return out;
    }

    /// The pruner counts references from the delta — the build must maintain the tally.
    bool needsRefCountDeltas() const noexcept override { return true; }

    /// EL-mode shallow-reorg hook (EthereumChainRollback.h): after a rollback to @p newHead the
    /// persistent counts/queue no longer describe the chain — blocks above newHead armed or
    /// consumed schedules that must be re-derived. The exact fix is a forced rebuild: discard the
    /// temporary overlays, re-walk the post-rollback roots and write the resulting counts back to
    /// the node rows. Costs a full head-trie walk per reorg, and is only paid when pruning is
    /// enabled (the default Noop observer keeps the hook a no-op). The startup window
    /// invariant (mpt_prune_window >= reorg_window, enforced by NodeConfig) guarantees every
    /// root in [newHead - N, newHead] is still fully resolvable on disk. The garbage sweep is
    /// NOT re-run: a reorg deletes no node rows itself, and the next boot's sweep policy
    /// covers whatever the rollback made unreachable.
    bcos::task::Task<void> coOnRollback(
        bcos::protocol::BlockNumber newHead, StateRootLookup stateRootAt) override
    {
        m_originalCounts.clear();
        m_stagedRaw.clear();
        m_stagedCounts.clear();
        m_stagedDeadlineErases.clear();
        m_stagedDeadlineInserts.clear();
        m_counts.clear();
        m_pending.clear();
        co_await init(newHead, m_executorVersion, std::move(stateRootAt), /*sweepGarbage=*/false,
            /*progress=*/{}, /*forceRebuild=*/true, m_allowRebuild);
    }

    /// After the block's WriteBatch: apply the staged overlay to the base tables and advance
    /// the in-memory watermark. coPreparePruneRows staged the block's counting work precisely so
    /// that a failed merge (which never reaches this hook) leaves the counts untouched and the
    /// commit retry reproduces the identical batch. Deletions already landed with the batch —
    /// there is nothing to hand off. The CommitObserver contract forbids throwing and blocking
    /// here.
    void onCommit(bcos::protocol::BlockNumber blockNumber, MPTDeltaLayer const& delta) override
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
        m_trackedCount = m_stagedTrackedCount;
        m_originalCounts.clear();
        m_stagedRaw.clear();
        m_stagedCounts.clear();
        m_stagedDeadlineErases.clear();
        m_stagedDeadlineInserts.clear();
        if (delta.stateRoot != bcos::h256{})
        {
            m_headRoot = delta.stateRoot;
        }

        auto current = m_watermark.load(std::memory_order_relaxed);
        while (current < blockNumber &&
               !m_watermark.compare_exchange_weak(current, blockNumber, std::memory_order_relaxed))
        {
        }
    }

    /// Highest committed block number this pruner has seen (currentBlock at init(), onCommit
    /// afterwards). −1 before either. The head root is persisted separately in the metadata.
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
        auto stored = bcos::task::syncWait(readStoredNode(hash));
        if (!stored)
        {
            return std::nullopt;
        }
        return stored->refCount;
    }

    /// The deletion deadline currently armed for @p hash, or nullopt when none.
    std::optional<uint64_t> deadlineOf(bcos::h256 const& hash) const
    {
        auto stored = bcos::task::syncWait(readStoredNode(hash));
        if (!stored || stored->deadline == 0)
        {
            return std::nullopt;
        }
        return stored->deadline;
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
    size_t trackedCount() const noexcept { return m_trackedCount; }

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

    struct H256FastHash
    {
        size_t operator()(bcos::h256 const& hash) const noexcept
        {
            size_t value;
            std::memcpy(&value, hash.data(), sizeof(value));
            return value;
        }
    };

    using StoredNodeMap = std::unordered_map<bcos::h256, StoredNode, H256FastHash>;

    static bcos::storage::Entry makeEntry(bcos::bytes value)
    {
        bcos::storage::Entry entry;
        entry.set(std::move(value));
        return entry;
    }

    static bcos::executor_v1::StateKey metadataKey(std::string_view key)
    {
        return {bcos::storage2::kMPTPruneMetaTable, key};
    }

    static bcos::executor_v1::StateKey queueStateKey(uint64_t deadline, bcos::h256 const& hash)
    {
        std::string key(8 + bcos::h256::SIZE, '\0');
        for (size_t i = 0; i < 8; ++i)
        {
            key[i] = static_cast<char>((deadline >> ((7 - i) * 8)) & 0xff);
        }
        std::memcpy(key.data() + 8, hash.data(), bcos::h256::SIZE);
        return {bcos::storage2::kMPTPruneQueueTable, key};
    }

    static std::pair<uint64_t, bcos::h256> decodeQueueKey(
        bcos::executor_v1::StateKeyView const& key)
    {
        if (key.m_key.size() != 8 + bcos::h256::SIZE)
        {
            BOOST_THROW_EXCEPTION(MPTInvariantViolation{}
                                  << bcos::errinfo_comment("invalid MPT prune queue key length"));
        }
        uint64_t deadline = 0;
        for (size_t i = 0; i < 8; ++i)
        {
            deadline = (deadline << 8) | static_cast<uint8_t>(key.m_key[i]);
        }
        return {deadline,
            bcos::h256(bcos::bytesConstRef(
                reinterpret_cast<bcos::byte const*>(key.m_key.data() + 8), bcos::h256::SIZE))};
    }

    bcos::task::Task<StoredNodeMap> readStoredNodes(std::vector<bcos::h256> const& hashes) const
    {
        StoredNodeMap result;
        if (hashes.empty())
        {
            co_return result;
        }
        auto keys = hashes | std::views::transform([](auto const& hash) {
            return bcos::ledger::mptNodeStateKey(hash);
        }) | std::ranges::to<std::vector>();
        auto const entries = co_await bcos::storage2::readSome(*m_backend, std::move(keys));
        for (size_t i = 0; i < entries.size(); ++i)
        {
            if (entries[i])
            {
                auto const raw = entries[i]->get();
                result.emplace(hashes[i], decodeStoredNode(raw));
            }
        }
        co_return result;
    }

    bcos::task::Task<std::optional<StoredNode>> readStoredNode(bcos::h256 const& hash) const
    {
        auto nodes = co_await readStoredNodes({hash});
        auto const it = nodes.find(hash);
        if (it == nodes.end())
        {
            co_return std::nullopt;
        }
        co_return std::move(it->second);
    }

    static bcos::storage::Entry makeEntry(std::string_view value)
    {
        bcos::storage::Entry entry;
        entry.set(value);
        return entry;
    }

    void appendMetadata(PruneRowBatch& out, uint64_t tracked, bcos::h256 const& headRoot) const
    {
        out.upserts.emplace_back(metadataKey(bcos::storage2::kMPTPruneTrackedCountKey),
            makeEntry(std::to_string(tracked)));
        out.upserts.emplace_back(metadataKey(bcos::storage2::kMPTPruneHeadRootKey),
            makeEntry(bcos::bytes(headRoot.begin(), headRoot.end())));
        out.upserts.emplace_back(metadataKey(bcos::storage2::kMPTPruneWindowKey),
            makeEntry(std::to_string(m_pruneWindow)));
    }

    struct MetadataState
    {
        bool present = false;
        bool matches = false;
        uint64_t tracked = 0;
    };

    bcos::task::Task<MetadataState> readMetadataState(bcos::h256 const& headRoot)
    {
        MetadataState state;
        auto trackedEntry = co_await bcos::storage2::readOne(
            *m_backend, metadataKey(bcos::storage2::kMPTPruneTrackedCountKey));
        auto rootEntry = co_await bcos::storage2::readOne(
            *m_backend, metadataKey(bcos::storage2::kMPTPruneHeadRootKey));
        auto windowEntry = co_await bcos::storage2::readOne(
            *m_backend, metadataKey(bcos::storage2::kMPTPruneWindowKey));
        state.present = trackedEntry && rootEntry && windowEntry;
        if (!state.present)
        {
            co_return state;
        }
        auto const trackedRaw = trackedEntry->get();
        auto const rootRaw = rootEntry->get();
        auto const windowRaw = windowEntry->get();
        state.tracked = std::stoull(std::string(trackedRaw));
        auto const storedWindow = std::stoll(std::string(windowRaw));
        state.matches =
            storedWindow == m_pruneWindow && rootRaw.size() == bcos::h256::SIZE &&
            bcos::h256(bcos::bytesConstRef(
                reinterpret_cast<bcos::byte const*>(rootRaw.data()), rootRaw.size())) == headRoot;
        co_return state;
    }

    bcos::task::Task<void> loadQueue()
    {
        auto iterator = co_await bcos::storage2::range(*m_backend, bcos::storage2::RANGE_SEEK,
            bcos::executor_v1::StateKey{bcos::storage2::kMPTPruneQueueTable, std::string_view{}});
        while (auto item = co_await iterator.next())
        {
            bcos::executor_v1::StateKeyView const key{std::get<0>(*item)};
            if (key.m_table != bcos::storage2::kMPTPruneQueueTable)
            {
                break;
            }
            auto const [deadline, hash] = decodeQueueKey(key);
            m_pending[deadline].insert(hash);
        }
    }

    bcos::task::Task<void> clearPersistentQueue()
    {
        auto iterator = co_await bcos::storage2::range(*m_backend, bcos::storage2::RANGE_SEEK,
            bcos::executor_v1::StateKey{bcos::storage2::kMPTPruneQueueTable, std::string_view{}});
        std::vector<bcos::executor_v1::StateKey> chunk;
        while (auto item = co_await iterator.next())
        {
            bcos::executor_v1::StateKeyView const key{std::get<0>(*item)};
            if (key.m_table != bcos::storage2::kMPTPruneQueueTable)
            {
                break;
            }
            chunk.push_back(std::get<0>(*item));
            if (chunk.size() >= SWEEP_DELETE_CHUNK)
            {
                co_await bcos::storage2::removeSome(*m_backend, std::move(chunk));
                chunk.clear();
            }
        }
        if (!chunk.empty())
        {
            co_await bcos::storage2::removeSome(*m_backend, std::move(chunk));
        }
    }

    bcos::task::Task<void> persistNodeCounts(std::vector<bcos::h256> const& hashes)
    {
        auto storedNodes = co_await readStoredNodes(hashes);
        std::vector<std::pair<bcos::executor_v1::StateKey, bcos::storage::Entry>> upserts;
        for (auto const& hash : hashes)
        {
            auto const stored = storedNodes.find(hash);
            if (stored == storedNodes.end())
            {
                BOOST_THROW_EXCEPTION(MPTInvariantViolation{} << bcos::errinfo_comment(
                                          "MPT pruning rebuild: reachable node row missing"));
            }
            auto const& computed = m_counts[hash];
            upserts.emplace_back(bcos::ledger::mptNodeStateKey(hash),
                makeEntry(encodeStoredNode(
                    bcos::ref(stored->second.rlp), computed.count, computed.deadline.value_or(0))));
        }
        co_await bcos::storage2::writeSome(*m_backend, std::move(upserts));
    }

    bcos::task::Task<void> persistRebuild(bcos::h256 const& headRoot)
    {
        std::vector<bcos::h256> hashes;
        for (auto const& [hash, entry] : m_counts)
        {
            hashes.push_back(hash);
            if (hashes.size() >= SWEEP_DELETE_CHUNK)
            {
                co_await persistNodeCounts(hashes);
                hashes.clear();
            }
        }
        if (!hashes.empty())
        {
            co_await persistNodeCounts(hashes);
        }

        co_await clearPersistentQueue();
        std::vector<std::pair<bcos::executor_v1::StateKey, bcos::storage::Entry>> queueUpserts;
        for (auto const& [deadline, bucket] : m_pending)
        {
            for (auto const& hash : bucket)
            {
                queueUpserts.emplace_back(
                    queueStateKey(deadline, hash), makeEntry(std::string_view{}));
                if (queueUpserts.size() >= SWEEP_DELETE_CHUNK)
                {
                    co_await bcos::storage2::writeSome(*m_backend, std::move(queueUpserts));
                    queueUpserts.clear();
                }
            }
        }
        if (!queueUpserts.empty())
        {
            co_await bcos::storage2::writeSome(*m_backend, std::move(queueUpserts));
        }

        PruneRowBatch metadata;
        m_trackedCount = m_counts.size();
        m_stagedTrackedCount = m_trackedCount;
        appendMetadata(metadata, m_trackedCount, headRoot);
        co_await bcos::storage2::writeSome(*m_backend, std::move(metadata.upserts));
    }

    static void descend(NodeReferences const& node, bool accountTrie,
        std::vector<std::pair<bcos::h256, bool>>& stack)
    {
        for (auto const& child : node.children)
        {
            stack.emplace_back(child, accountTrie);
        }
        if (accountTrie && node.accountLeaf && node.storageRoot &&
            *node.storageRoot != emptyRootHash())
        {
            stack.emplace_back(*node.storageRoot, false);
        }
    }

    /// Node-read batch size for walkTrie: 64 covers a branch node's 16-way sibling fan-out
    /// several times over (the stack's steady-state occupancy right after a descend), so the
    /// walk costs a handful of MultiGets per trie level without ballooning the transient
    /// key/result vectors.
    static constexpr size_t WALK_READ_BATCH = 64;

    /// In-flight sub-batch fetches in walkTrie's pipeline. The per-node rebuild cost is
    /// dominated by the 64KB data-block read + decompress behind every hash-random key —
    /// synchronous, single-threaded work one MultiGet serializes. The walk keeps up to
    /// WALK_READ_WAYS sub-batches' MultiGets running CONCURRENTLY as tbb::task_group tasks,
    /// multiplying the startup-rebuild throughput on multi-core hosts; 8 outstanding fetches
    /// bound the I/O and page-cache pressure on archive-scale databases.
    static constexpr size_t WALK_READ_WAYS = 8;

    /// The single trie-traversal implementation behind BOTH rebuild phases (countWalk and
    /// deadlineWalk below are thin wrappers supplying the per-node callback): iterative over an
    /// explicit (hash, accountTrie) stack, descended per descend(). @p onNode runs per POPPED
    /// hash and returns whether to read and descend into the node (false = already accounted
    /// for — skip the read and the whole subtree). The empty root short-circuits: an empty
    /// trie stores no nodes.
    ///
    /// Node rows are fetched in BATCHES: up to WALK_READ_BATCH entries are popped per
    /// sub-batch, each run through @p onNode in pop order, and the accepted rows are fetched
    /// with one batched MultiGet instead of a point read per node — the dominant startup-rebuild
    /// cost on a cold RocksDB.
    /// Collection, fetch and application are PIPELINED: each sub-batch's fetch (the blocking
    /// MultiGet AND the RLP decode of its rows, both inside the tbb task) is launched as soon
    /// as the sub-batch is collected, up to WALK_READ_WAYS fetches stay in flight, and the
    /// decoded nodes are applied (descended into) strictly in collection order — each
    /// application waits only on its OWN sub-batch's fetch, so there is no per-round barrier
    /// and later fetches overlap earlier applications. Batching changes the VISIT ORDER versus
    /// the serial per-node walk (siblings are popped before the earlier entries' children are
    /// pushed), and both phases are correct under any visit order: Phase 1 counts every
    /// encounter, so any order yields the same per-hash tally, and Phase 2's try_emplace makes
    /// the visited set a reachability closure, likewise order-independent. The same hash may be
    /// accepted several times within one sub-batch (two accounts sharing a storage trie push
    /// the same storageRoot); duplicate keys are fetched and decoded ONCE per sub-batch, then
    /// descended once per accepted entry, so the per-encounter tally is preserved without
    /// relying on MultiGet's duplicate-key contract. The fail-loud contract is unchanged: a
    /// reachable row missing from the batch's results throws the same MPTInvariantViolation,
    /// captured inside the fetch task and rethrown on the walk's thread.
    struct WalkStats
    {
        uint64_t batches = 0;
        uint64_t nodeReads = 0;
        uint64_t readMicros = 0;
        uint64_t decodeMicros = 0;
        uint64_t waitMicros = 0;
    };

    static uint64_t elapsedMicros(std::chrono::steady_clock::time_point start)
    {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start)
                .count());
    }

    template <typename OnNode>
    bcos::task::Task<void> walkTrie(
        bcos::h256 root, bool accountTrie, OnNode&& onNode, WalkStats* stats = nullptr)
    {
        if (root == emptyRootHash())
        {
            co_return;
        }
        std::vector<std::pair<bcos::h256, bool>> stack{{root, accountTrie}};
        // Pipeline slots: WALK_READ_WAYS sub-batches in flight, applied strictly in collection
        // order. Each slot's task_group owns ONE fetch task at a time — the blocking MultiGet
        // plus the RLP decode of every fetched row — and is reused after wait().
        struct Way
        {
            oneapi::tbb::task_group fetch;
            std::vector<std::pair<bcos::h256, bool>> accepted;
            std::vector<size_t> resultIndex;
            std::vector<bcos::h256> keyHashes;
            std::vector<bcos::executor_v1::StateKey> keys;
            std::vector<bool> decodeAccount;
            std::vector<std::optional<NodeReferences>> decoded;
            uint64_t readMicros = 0;
            uint64_t decodeMicros = 0;
            std::exception_ptr error;
        };
        std::vector<Way> ways(WALK_READ_WAYS);
        std::unordered_map<bcos::h256, size_t, H256FastHash> dedup;
        size_t head = 0;  // oldest in-flight sub-batch, next to apply
        size_t tail = 0;  // next slot to collect into
        size_t inFlight = 0;
        // A throw anywhere below (onNode, a rethrown fetch error) must first join the
        // in-flight fetch tasks: they reference this frame.
        try
        {
            while (!stack.empty() || inFlight > 0)
            {
                // Collect and launch sub-batches while the window has room. onNode still runs
                // on this thread at pop time, so its side effects (the phase's counts/visited
                // set) stay single-threaded. Their ORDER is NOT the serial walk's — batching
                // pops siblings before the earlier entries' children are pushed (see the
                // visit-order note above); both phases are order-independent by design.
                while (!stack.empty() && inFlight < WALK_READ_WAYS)
                {
                    auto& way = ways[tail];
                    way.accepted.clear();
                    way.resultIndex.clear();
                    way.keyHashes.clear();
                    way.keys.clear();
                    way.decodeAccount.clear();
                    dedup.clear();
                    for (size_t popped = 0; popped < WALK_READ_BATCH && !stack.empty(); ++popped)
                    {
                        auto const [hash, isAccount] = stack.back();
                        stack.pop_back();
                        if (!onNode(hash))
                        {
                            continue;
                        }
                        auto const [it, inserted] = dedup.try_emplace(hash, way.keyHashes.size());
                        if (inserted)
                        {
                            way.keyHashes.push_back(hash);
                            way.decodeAccount.push_back(false);
                            way.keys.push_back(bcos::ledger::mptNodeStateKey(hash));
                        }
                        way.decodeAccount[it->second] = way.decodeAccount[it->second] || isAccount;
                        way.accepted.emplace_back(hash, isAccount);
                        way.resultIndex.push_back(it->second);
                    }
                    if (way.accepted.empty())
                    {
                        continue;
                    }
                    way.error = nullptr;
                    way.decoded.clear();
                    way.decoded.resize(way.keyHashes.size());
                    way.readMicros = 0;
                    way.decodeMicros = 0;
                    if (stats)
                    {
                        ++stats->batches;
                        stats->nodeReads += way.keyHashes.size();
                    }
                    way.fetch.run([this, &way] {
                        try
                        {
                            auto const readStart = std::chrono::steady_clock::now();
                            auto const entries = bcos::task::tbb::syncWait(
                                bcos::storage2::readSome(*m_backend, std::move(way.keys)));
                            way.readMicros = elapsedMicros(readStart);
                            auto const decodeStart = std::chrono::steady_clock::now();
                            for (size_t slot = 0; slot < entries.size(); ++slot)
                            {
                                auto const& entry = entries[slot];
                                if (!entry)
                                {
                                    BOOST_THROW_EXCEPTION(
                                        MPTInvariantViolation{} << bcos::errinfo_comment(
                                            "MPT pruning rebuild: reachable node row missing "
                                            "from the committed backend (hash " +
                                            way.keyHashes[slot].abridged() + ")"));
                                }
                                auto const raw = entry->get();
                                auto stored = decodeStoredNode(raw);
                                way.decoded[slot] = decodeNodeReferences(
                                    bcos::ref(stored.rlp), way.decodeAccount[slot]);
                            }
                            way.decodeMicros = elapsedMicros(decodeStart);
                        }
                        catch (...)
                        {
                            way.error = std::current_exception();
                        }
                    });
                    tail = (tail + 1) % WALK_READ_WAYS;
                    ++inFlight;
                }

                // Apply the oldest in-flight sub-batch in collection order, exactly the
                // serial walk's descend; waiting blocks only on THIS sub-batch's fetch.
                if (inFlight == 0)
                {
                    break;  // the last pops were all rejected by onNode; nothing to apply
                }
                auto& way = ways[head];
                auto const waitStart = std::chrono::steady_clock::now();
                way.fetch.wait();
                if (stats)
                {
                    stats->waitMicros += elapsedMicros(waitStart);
                    stats->readMicros += way.readMicros;
                    stats->decodeMicros += way.decodeMicros;
                }
                if (way.error)
                {
                    std::rethrow_exception(way.error);
                }
                for (size_t i = 0; i < way.accepted.size(); ++i)
                {
                    descend(*way.decoded[way.resultIndex[i]], way.accepted[i].second, stack);
                }
                head = (head + 1) % WALK_READ_WAYS;
                --inFlight;
            }
        }
        catch (...)
        {
            for (size_t i = 0; i < inFlight; ++i)
            {
                ways[(head + i) % WALK_READ_WAYS].fetch.wait();
            }
            throw;
        }
    }

    /// Phase 1: count EVERY encounter of each hash-addressed node reachable from @p root (no
    /// dedup — K referencing tries are K live references) and always descend. The counted keys
    /// double as Phase 2's seen set: deadlineWalk dedups against m_counts itself, so no separate
    /// set is kept.
    bcos::task::Task<void> countWalk(bcos::h256 root, bool accountTrie)
    {
        auto const start = std::chrono::steady_clock::now();
        uint64_t visited = 0;
        WalkStats stats;
        co_await walkTrie(
            root, accountTrie,
            [this, &visited](bcos::h256 const& hash) {
                ++m_counts[hash].count;
                if (++visited % REBUILD_LOG_INTERVAL == 0)
                {
                    MPT_PRUNER_LOG(INFO)
                        << "MPT pruning: rebuild phase 1 progress" << LOG_KV("nodes", visited)
                        << LOG_KV("tracked", m_counts.size());
                }
                return true;
            },
            &stats);
        MPT_PRUNER_LOG(INFO) << "MPT pruning: rebuild phase 1 finished" << LOG_KV("nodes", visited)
                             << LOG_KV("tracked", m_counts.size())
                             << LOG_KV("batches", stats.batches)
                             << LOG_KV("nodeReads", stats.nodeReads)
                             << LOG_KV("elapsedMs", elapsedMicros(start) / 1000)
                             << LOG_KV("readTotalMs", stats.readMicros / 1000)
                             << LOG_KV("decodeTotalMs", stats.decodeMicros / 1000)
                             << LOG_KV("waitMs", stats.waitMicros / 1000);
    }

    /// Phase 2: walk @p root's trie, deduping against m_counts itself (whose key set covers the
    /// head state plus the newer roots already walked in this phase) — an already-counted hash
    /// is neither re-armed nor descended into. A node with no entry yet is one the head state
    /// no longer references, so its fresh entry (count 0) is armed with @p deadline = s+1+N,
    /// s the walked root's block.
    bcos::task::Task<void> deadlineWalk(bcos::h256 root, bool accountTrie, uint64_t deadline)
    {
        co_await walkTrie(root, accountTrie, [this, deadline](bcos::h256 const& hash) {
            auto const [it, inserted] = m_counts.try_emplace(hash);
            if (!inserted)
            {
                return false;
            }
            it->second.deadline = deadline;
            m_pending[deadline].insert(hash);
            return true;
        });
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
    bool m_allowRebuild = true;
    // The chain's executor_version, captured by init and replayed by coOnRollback's rebuild
    // (the lane — legacy vs Ethereum — decides firstMptBlock; it is genesis-fixed, so the
    // init-time value stays correct across a reorg).
    int64_t m_executorVersion = 0;
    std::atomic<int64_t> m_watermark{-1};
    uint64_t m_lastSweepDeleted = 0;
    uint64_t m_trackedCount = 0;
    uint64_t m_stagedTrackedCount = 0;
    bcos::h256 m_headRoot;
    std::unordered_map<bcos::h256, Entry, H256FastHash> m_counts;
    std::map<uint64_t, std::unordered_set<bcos::h256>> m_pending;

    std::unordered_map<bcos::h256, Entry, H256FastHash> m_originalCounts;
    std::unordered_map<bcos::h256, bcos::bytes, H256FastHash> m_stagedRaw;
    std::unordered_map<bcos::h256, Entry, H256FastHash> m_stagedCounts;
    std::map<uint64_t, std::unordered_set<bcos::h256>> m_stagedDeadlineErases;
    std::map<uint64_t, std::unordered_set<bcos::h256>> m_stagedDeadlineInserts;
};

}  // namespace bcos::ledger::mpt
