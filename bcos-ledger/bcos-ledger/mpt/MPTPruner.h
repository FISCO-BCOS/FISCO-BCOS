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
 * @brief MPTPruner — the reference-counting, windowed-deletion CommitObserver: per-block
 *        metadata rows prepared for the block's own WriteBatch, node deletion deferred to a
 *        private worker thread (spec §4.8, §5.6)
 */
#pragma once

#include "CommitObserver.h"
#include "PruneMetadata.h"
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/GenesisStateRoot.h>
#include <bcos-task/Task.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <bcos-utilities/Log.h>
#include <boost/exception/diagnostic_information.hpp>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <range/v3/view/transform.hpp>
#include <ranges>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bcos::ledger::mpt
{

namespace detail
{
/// The pruner's private strand: one background thread draining a FIFO of deletion jobs, so
/// onCommit never runs storage I/O on the commit path. Jobs are idempotent deletion passes, so
/// a dropped job (queue overflow is impossible — the deque is unbounded — but post() still
/// never throws: an OOM enqueue drops the work and the next commit re-posts it) never loses
/// correctness, only timeliness.
class AsyncWorker
{
public:
    AsyncWorker();
    ~AsyncWorker();  // drains queued jobs, then stops and joins
    AsyncWorker(AsyncWorker const&) = delete;
    AsyncWorker(AsyncWorker&&) = delete;
    AsyncWorker& operator=(AsyncWorker const&) = delete;
    AsyncWorker& operator=(AsyncWorker&&) = delete;

    /// Enqueue @p job. Never throws (the CommitObserver contract forbids it on the commit path).
    void post(std::function<void()> job) noexcept;

    /// Returns after every job queued so far has run. Test/drain hook — production code never
    /// waits for the pruner.
    void waitForIdle();

private:
    void run();

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<std::function<void()>> m_jobs;
    bool m_stop{false};
    std::thread m_thread;
};
}  // namespace detail

/// Reference-counting MPT pruning (spec §4.8), one instance per chain over the committed-state
/// backend (production: GlobalStateStorage::latestBackend()).
///
/// Counting rule, applied per block from the block's MPTDeltaLayer:
///  - every emission of a node (each trie build that produced it — MPTDeltaLayer::refCountDeltas,
///    NOT the deduplicated newNodes map) is one reference CREATION: refcount +1;
///  - every obsoletion is one reference removal: refcount −1, saturating at 0 (genesis-prewrite
///    and pre-activation nodes never passed a delta, so their count starts at 0 and their first
///    obsoletion is a 0→0 that still queues them);
///  - a count dropping to 0 schedules the node for deletion at blockNumber + pruneWindow (a queue
///    row); a count rising back above 0 before then revokes the schedule (the stale queue row is
///    cleaned lazily by the deletion pass, which re-reads the refcount row before deleting).
///
/// Split across the two CommitObserver hooks:
///  - coPreparePruneRows (inside the commit coroutine, before the layer merge): one batched
///    readSome of the touched refcount rows, the rule application in memory, and the resulting
///    metadata rows handed back — the commit flow writes them into prewriteStorage so metadata
///    and block data land in ONE WriteBatch (no "data persisted, metadata lost" crash window);
///  - onCommit (after the WriteBatch): advances the in-memory watermark and posts a deletion
///    pass to the private worker. Never throws, never does I/O.
///
/// The deletion pass prefix-scans the queue table (keys are BE-u64 targetBlock first, so
/// iteration is deadline-ordered), re-checks each candidate's refcount row (count == 0 AND
/// pendingDeleteAt == the queue row's targetBlock — anything else is a stale queue entry left by
/// a revival), then removeSome's the node rows DIRECTLY on the backend (no cache layer may sit
/// between) and finally removes the consumed metadata rows. Deleting an absent key is a no-op,
/// so the pass is idempotent: after a crash, init() re-reads the persisted watermark and replays
/// every deletion the watermark covers.
///
/// Window guarantee: a node referenced by the state of block r can only be obsoleted at some
/// block o > r, so its deletion runs at o + N >= r + N + 1 — every state root in
/// [head − N, head] keeps its full node set on disk (N + 1 provable states).
///
/// @tparam Backend a storage2 ReadWriteStorage over (executor_v1::StateKey → storage::Entry)
///         with physical (non-logical) removeSome and RANGE_SEEK support — RocksDBStorage2 and
///         an ORDERED MemoryStorage both qualify.
template <class Backend>
class MPTPruner : public CommitObserver
{
public:
    static constexpr size_t DEFAULT_DELETE_BATCH_SIZE = 1000;

    /// @param backend         the committed-state backend; every read and delete below hits it
    ///                        directly. Must outlive the pruner.
    /// @param pruneWindow     N: a node whose refcount hits 0 at block b becomes deletable once
    ///                        block b + N is committed.
    /// @param deleteBatchSize max queue rows consumed per scan batch (deletion round-trips).
    MPTPruner(Backend& backend, int64_t pruneWindow,
        size_t deleteBatchSize = DEFAULT_DELETE_BATCH_SIZE)
      : m_backend(std::addressof(backend)),
        m_pruneWindow(pruneWindow),
        m_deleteBatchSize(deleteBatchSize == 0 ? 1 : deleteBatchSize)
    {}

    /// Startup recovery: read the persisted watermark (absent on a chain that never pruned —
    /// treated as "nothing committed yet") and post a replay pass for everything it covers.
    /// Deletions are idempotent, so replaying rows an earlier incarnation already consumed is
    /// harmless. @throws MPTDecodeError on a corrupted watermark row — fail loudly at boot.
    bcos::task::Task<void> init()
    {
        auto entry = co_await bcos::storage2::readOne(*m_backend, watermarkKey());
        if (!entry)
        {
            co_return;
        }
        auto raw = entry->get();
        auto const persisted = decodeWatermark(
            bcos::bytesConstRef(reinterpret_cast<bcos::byte const*>(raw.data()), raw.size()));
        m_watermark.store(static_cast<int64_t>(persisted), std::memory_order_relaxed);
        scheduleDeletion(static_cast<bcos::protocol::BlockNumber>(persisted));
    }

    /// The metadata rows for @p blockNumber's delta, for the block's own WriteBatch. Pure
    /// computation over one batched refcount read; issues no writes itself.
    bcos::task::Task<std::vector<std::pair<bcos::executor_v1::StateKey, bcos::storage::Entry>>>
        coPreparePruneRows(
            bcos::protocol::BlockNumber blockNumber, MPTDeltaLayer const& delta) override
    {
        // Per-hash net reference movement. buildAndCollect tallies refCountDeltas through
        // mergeNodeDelta; a delta that left it empty (hand-built, or a future producer) falls
        // back to the set reading: +1 per newNodes hash, −1 per obsoleted/intraBlock hash. The
        // set reading cannot see byte-identical re-emits (mergeTrie reports them only in
        // TrieMergeResult::reemittedNodes, which the layer does not carry), so it over-counts
        // no-net-change rebuilds — production deltas always carry refCountDeltas.
        std::unordered_map<bcos::h256, int64_t> const& netDeltas = delta.refCountDeltas;
        std::unordered_map<bcos::h256, int64_t> derived;
        if (netDeltas.empty())
        {
            for (auto const& hash : delta.newNodes | std::views::keys)
            {
                ++derived[hash];
            }
            for (auto const& hash : delta.obsoletedNodes)
            {
                --derived[hash];
            }
            for (auto const& hash : delta.intraBlockObsoleted)
            {
                --derived[hash];
            }
        }
        auto const& movements = netDeltas.empty() ? derived : netDeltas;

        std::vector<std::pair<bcos::executor_v1::StateKey, bcos::storage::Entry>> rows;
        // The watermark advances with EVERY block, delta or not: it is the crash-replay
        // horizon the deletion pass and init() share.
        storage::Entry watermarkEntry;
        watermarkEntry.set(encodeWatermark(static_cast<uint64_t>(blockNumber)));
        rows.emplace_back(watermarkKey(), std::move(watermarkEntry));

        if (movements.empty())
        {
            co_return rows;
        }

        std::vector<bcos::h256> hashes;
        hashes.reserve(movements.size());
        for (auto const& hash : movements | std::views::keys)
        {
            hashes.push_back(hash);
        }
        auto const refEntries = co_await bcos::storage2::readSome(
            *m_backend, hashes | ::ranges::views::transform(
                                   [](auto const& hash) { return pruneRefKey(hash); }));

        for (size_t i = 0; i < hashes.size(); ++i)
        {
            auto const& hash = hashes[i];
            int64_t const movement = movements.at(hash);
            PruneRefCount refCount{};
            if (refEntries[i])
            {
                auto raw = refEntries[i]->get();
                refCount = decodeRefCount(bcos::bytesConstRef(
                    reinterpret_cast<bcos::byte const*>(raw.data()), raw.size()));
            }
            uint64_t const oldCount = refCount.count;
            uint64_t const newCount =
                    static_cast<uint64_t>(std::max<int64_t>(
                        0, static_cast<int64_t>(oldCount) + movement));
            bool const wasObsoleted =
                delta.obsoletedNodes.contains(hash) || delta.intraBlockObsoleted.contains(hash);

            bool changed = (newCount != oldCount);
            if (newCount == 0 && wasObsoleted && !refCount.pendingDeleteAt)
            {
                // >0→0, or the saturating 0→0 of a node that never passed a delta (genesis
                // prewrite / pre-activation rows): schedule the deletion.
                refCount.pendingDeleteAt =
                    static_cast<uint64_t>(blockNumber + m_pruneWindow);
                changed = true;
                storage::Entry queueEntry;
                queueEntry.set(std::string{});
                rows.emplace_back(
                    pruneQueueKey(*refCount.pendingDeleteAt, hash), std::move(queueEntry));
            }
            else if (newCount > 0 && refCount.pendingDeleteAt)
            {
                // 0→>0: revived before its deletion ran — revoke the schedule. The queue row
                // itself is cleaned lazily by the deletion pass's refcount re-check.
                refCount.pendingDeleteAt.reset();
                changed = true;
            }
            if (!changed)
            {
                continue;
            }
            refCount.count = newCount;
            storage::Entry refEntry;
            refEntry.set(encodeRefCount(refCount));
            rows.emplace_back(pruneRefKey(hash), std::move(refEntry));
        }
        co_return rows;
    }

    /// After the block's WriteBatch: advance the watermark, hand the deletion pass to the
    /// worker, return. The CommitObserver contract forbids throwing and blocking here.
    void onCommit(
        bcos::protocol::BlockNumber blockNumber, MPTDeltaLayer const& /*delta*/) override
    {
        try
        {
            auto current = m_watermark.load(std::memory_order_relaxed);
            while (current < blockNumber &&
                   !m_watermark.compare_exchange_weak(
                       current, blockNumber, std::memory_order_relaxed))
            {
            }
            scheduleDeletion(blockNumber);
        }
        catch (...)
        {
            // Deletion is idempotent and re-posted by the next commit (and by init() after a
            // restart): a dropped pass is a timeliness issue, never a correctness one.
        }
    }

    /// One full deletion pass: consume every queue row whose targetBlock <= @p upToBlock.
    /// Idempotent; exposed (not just worker-driven) so tests and init() can run it directly.
    bcos::task::Task<void> coDeleteExpired(bcos::protocol::BlockNumber upToBlock)
    {
        if (upToBlock < 0)
        {
            co_return;
        }
        auto const horizon = static_cast<uint64_t>(upToBlock);
        struct Candidate
        {
            bcos::executor_v1::StateKey queueKey;
            uint64_t targetBlock;
            bcos::h256 hash;
        };
        while (true)
        {
            // Queue keys are (BE-u64 targetBlock ‖ hash) inside one table, so the seek lands on
            // the oldest deadline and iteration stops at the first row beyond the horizon.
            std::vector<Candidate> batch;
            auto iterator = co_await bcos::storage2::range(*m_backend, bcos::storage2::RANGE_SEEK,
                bcos::executor_v1::StateKey{kPruneQueueTable, std::string_view{}});
            while (auto item = co_await iterator.next())
            {
                auto const& key = std::get<0>(*item);
                auto const& value = std::get<1>(*item);
                bcos::executor_v1::StateKeyView const keyView{key};
                if (keyView.m_table != kPruneQueueTable)
                {
                    break;
                }
                if (!std::get_if<storage::Entry>(std::addressof(value)))
                {
                    continue;  // tombstone on a logical-deletion backend: not a live queue row
                }
                auto [targetBlock, hash] = decodeQueueKeyPart(keyView.m_key);
                if (targetBlock > horizon)
                {
                    break;
                }
                batch.push_back(Candidate{.queueKey = bcos::executor_v1::StateKey{key},
                    .targetBlock = targetBlock,
                    .hash = hash});
                if (batch.size() >= m_deleteBatchSize)
                {
                    break;
                }
            }
            if (batch.empty())
            {
                co_return;
            }

            // Re-check before deleting: a queue row is only a hint — the refcount row is the
            // verdict. count == 0 AND pendingDeleteAt == this row's targetBlock confirms the
            // schedule was never revoked or re-armed (a re-armed node carries a NEWER
            // pendingDeleteAt, mismatching this stale row).
            auto const refEntries = co_await bcos::storage2::readSome(*m_backend,
                batch | ::ranges::views::transform(
                            [](auto const& candidate) { return pruneRefKey(candidate.hash); }));
            std::vector<bcos::executor_v1::StateKey> nodeKeys;
            std::vector<bcos::executor_v1::StateKey> metadataKeys;
            for (size_t i = 0; i < batch.size(); ++i)
            {
                bool confirmed = false;
                if (refEntries[i])
                {
                    auto raw = refEntries[i]->get();
                    auto const refCount = decodeRefCount(bcos::bytesConstRef(
                        reinterpret_cast<bcos::byte const*>(raw.data()), raw.size()));
                    confirmed = refCount.count == 0 &&
                                refCount.pendingDeleteAt == batch[i].targetBlock;
                }
                if (confirmed)
                {
                    nodeKeys.push_back(bcos::ledger::mptNodeStateKey(batch[i].hash));
                    metadataKeys.push_back(pruneRefKey(batch[i].hash));
                }
                metadataKeys.push_back(std::move(batch[i].queueKey));
            }
            // Node rows first, metadata second: a crash between the two replays a queue row
            // whose re-check now sees a missing refcount row (not confirmed) and simply cleans
            // the row up. The reverse order would replay a confirmed deletion — also safe,
            // removeSome of an absent key is a no-op — but this order never deletes a node
            // whose metadata says nothing.
            if (!nodeKeys.empty())
            {
                co_await bcos::storage2::removeSome(*m_backend, nodeKeys);
            }
            co_await bcos::storage2::removeSome(*m_backend, metadataKeys);
        }
    }

    /// Highest committed block number this pruner has seen (persisted watermark at init(),
    /// onCommit afterwards). −1 before either.
    bcos::protocol::BlockNumber watermark() const noexcept
    {
        return m_watermark.load(std::memory_order_relaxed);
    }

    /// Test hook: returns after every deletion pass queued so far has finished.
    void waitForIdle() { m_worker.waitForIdle(); }

private:
    void scheduleDeletion(bcos::protocol::BlockNumber upToBlock)
    {
        m_worker.post([this, upToBlock]() {
            try
            {
                bcos::task::syncWait(coDeleteExpired(upToBlock));
            }
            catch (std::exception const& e)
            {
                BCOS_LOG(WARNING) << "MPTPruner deletion pass failed: "
                                  << boost::diagnostic_information(e);
            }
            catch (...)
            {
                BCOS_LOG(WARNING) << "MPTPruner deletion pass failed with an unknown error";
            }
        });
    }

    Backend* m_backend;
    int64_t m_pruneWindow;
    size_t m_deleteBatchSize;
    std::atomic<int64_t> m_watermark{-1};
    // Declared LAST so it is destroyed FIRST: its destructor drains and joins the worker while
    // every member a running job may touch is still alive.
    detail::AsyncWorker m_worker;
};

}  // namespace bcos::ledger::mpt
