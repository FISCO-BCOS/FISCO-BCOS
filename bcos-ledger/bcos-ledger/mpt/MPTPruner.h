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
 *        metadata rows AND expired-node deletions prepared inside the commit coroutine,
 *        landing in the block's own WriteBatch (spec §4.8, §5.6)
 */
#pragma once

#include "CommitObserver.h"
#include "PruneMetadata.h"
#include <bcos-framework/ledger/Features.h>
#include <bcos-framework/ledger/FeaturesStorage.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/GenesisStateRoot.h>
#include <bcos-task/Task.h>
#include <bcos-tool/Exceptions.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <range/v3/view/transform.hpp>
#include <ranges>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bcos::ledger::mpt
{

/// Reference-counting MPT pruning (spec §4.8), one instance per chain over the committed-state
/// backend (production: GlobalStateStorage::latestBackend()).
///
/// Counting rule, applied per block from the block's MPTDeltaLayer:
///  - every emission of a node (each trie build that produced it — MPTDeltaLayer::refCountDeltas,
///    NOT the deduplicated newNodes map) is one reference CREATION: refcount +1;
///  - every obsoletion is one reference removal: refcount −1, saturating at 0. The saturating
///    0→0 obsoletion still queues the node: with genesis seeding (writePruneSeedRows) and the
///    init() startup guard in place it can only fire for a hand-built delta in tests — nodes
///    created before tracking started are exactly what the guard refuses to run against;
///  - a count dropping to 0 schedules the node for deletion at blockNumber + pruneWindow (a queue
///    row); a count rising back above 0 before then revokes the schedule (the stale queue row is
///    cleaned lazily when the deletion consumption reaches it).
///
/// Everything happens in coPreparePruneRows, inside the commit coroutine under the commit mutex,
/// BEFORE the block's storage layers merge:
///  1. one batched readSome of the touched refcount rows, the rule application in memory — the
///     resulting metadata rows go into the returned batch's `rows`;
///  2. consumption of the delete queue up to the current block (prefix scan — queue keys are
///     BE-u64 targetBlock first, so iteration is deadline-ordered — capped at
///     m_deleteBatchSize rows per block; a backlog continues next block): each candidate is
///     re-checked against its refcount row AS UPDATED BY THIS BLOCK (the in-memory overlay from
///     step 1 — a node this very block revived reads as count > 0 and its expired queue row is
///     dropped instead of deleting a live node), and confirmed deletions (count == 0 AND
///     pendingDeleteAt == the queue row's targetBlock — anything else is a stale entry left by
///     a revival or a re-arm) go into the batch's `deletions`: the "/mpt/" node row, the
///     refcount row, and the consumed queue row.
/// The commit flow applies rows + deletions to prewriteStorage, so block data, pruning metadata
/// and node deletions land in ONE WriteBatch: no crash window between "node deleted" and
/// "metadata persisted", and no worker thread racing a concurrent commit (the F2 review fix —
/// an earlier revision deleted from a private thread and could remove a node a concurrent block
/// had just revived).
///
/// Window guarantee: a node referenced by the state of block r can only be obsoleted at some
/// block o > r, so its deletion is consumed at o + N >= r + N + 1 — every state root in
/// [head − N, head] keeps its full node set on disk (N + 1 provable states).
///
/// Known gap: when an account is deleted outright (tombstone path, MPTBuilder), only its storage
/// ROOT is obsoleted and counted down; the subtree below it is not cascade-walked, so those
/// nodes leak until the account-deletion path actually appears (today no protocol operation
/// deletes a pre-existing account — EIP-6780). Birth-side counting is naturally exact.
///
/// @tparam Backend a storage2 ReadWriteStorage over (executor_v1::StateKey → storage::Entry)
///         readable through StateKeyView as well (Features::readFromStorage), with ordered
///         RANGE_SEEK support — RocksDBStorage2 and an ORDERED MemoryStorage both qualify.
template <class Backend>
class MPTPruner : public CommitObserver
{
public:
    static constexpr size_t DEFAULT_DELETE_BATCH_SIZE = 1000;

    /// @param backend         the committed-state backend; every read below hits it directly
    ///                        (no cache layer may sit between). Must outlive the pruner.
    /// @param pruneWindow     N: a node whose refcount hits 0 at block b becomes deletable once
    ///                        block b + N is committed.
    /// @param deleteBatchSize max queue rows consumed per block (deletion round-trips).
    MPTPruner(Backend& backend, int64_t pruneWindow,
        size_t deleteBatchSize = DEFAULT_DELETE_BATCH_SIZE)
      : m_backend(std::addressof(backend)),
        m_pruneWindow(pruneWindow),
        m_deleteBatchSize(deleteBatchSize == 0 ? 1 : deleteBatchSize)
    {}

    /// Startup guard and watermark recovery. @p currentBlock is the ledger's current block
    /// number at boot. Pruning counts only what it sees from the MPT's first block on, so
    /// enabling it later than that deletes live state — refuse:
    ///  - watermark present: it must equal @p currentBlock, otherwise blocks committed while
    ///    pruning was disabled left an uncounted gap ("disabled for a while, re-enabled");
    ///  - no watermark, currentBlock == 0: a fresh chain — allowed, but an L2 chain's genesis
    ///    must carry the seeded refcount rows (writePruneSeedRows; genesis written by a binary
    ///    predating seeding is rejected);
    ///  - no watermark, currentBlock > 0: allowed only if no MPT block has committed yet —
    ///    i.e. a non-MPT chain, or scenario A exactly at/before the feature_mpt_state_root
    ///    activation block. An L2 chain (MPT from block 1) is always past that point here.
    /// @throws bcos::tool::InvalidConfig on any refusal, naming both block numbers;
    ///         MPTDecodeError on a corrupted watermark row — both fail loudly at boot.
    bcos::task::Task<void> init(bcos::protocol::BlockNumber currentBlock)
    {
        auto entry = co_await bcos::storage2::readOne(*m_backend, watermarkKey());
        if (entry)
        {
            auto raw = entry->get();
            auto const persisted = decodeWatermark(
                bcos::bytesConstRef(reinterpret_cast<bcos::byte const*>(raw.data()), raw.size()));
            if (persisted != static_cast<uint64_t>(currentBlock))
            {
                BOOST_THROW_EXCEPTION(bcos::tool::InvalidConfig{}
                                      << bcos::errinfo_comment(
                                             "MPT pruning startup guard: persisted watermark " +
                                             std::to_string(persisted) + " != current block " +
                                             std::to_string(currentBlock) +
                                             " — pruning was disabled for the blocks in between "
                                             "and their node deltas are uncounted; refusing to "
                                             "start with storage.mpt_prune_window enabled"));
            }
            m_watermark.store(static_cast<int64_t>(persisted), std::memory_order_relaxed);
            co_return;
        }

        bcos::ledger::Features features;
        co_await features.readFromStorage(*m_backend, currentBlock);
        using Flag = bcos::ledger::Features::Flag;
        if (currentBlock > 0)
        {
            if (features.get(Flag::feature_l2_ethereum_compat))
            {
                BOOST_THROW_EXCEPTION(bcos::tool::InvalidConfig{}
                                      << bcos::errinfo_comment(
                                             "MPT pruning startup guard: cannot enable "
                                             "storage.mpt_prune_window at current block " +
                                             std::to_string(currentBlock) +
                                             " on an L2 chain — its MPT has been building since "
                                             "block 1 and those node deltas are uncounted"));
            }
            if (features.get(Flag::feature_mpt_state_root))
            {
                auto const activation =
                    features.activationBlockOf(Flag::feature_mpt_state_root);
                if (currentBlock > activation)
                {
                    BOOST_THROW_EXCEPTION(bcos::tool::InvalidConfig{}
                                          << bcos::errinfo_comment(
                                                 "MPT pruning startup guard: cannot enable "
                                                 "storage.mpt_prune_window at current block " +
                                                 std::to_string(currentBlock) +
                                                 " — feature_mpt_state_root activated at block " +
                                                 std::to_string(activation) +
                                                 " and the MPT blocks since then are uncounted"));
                }
            }
            co_return;
        }

        // currentBlock == 0: fresh chain. An L2 chain's genesis nodes were written without a
        // delta, so their refcount rows must have been seeded with the genesis state; a genesis
        // written by a binary predating seeding has no marker — refuse rather than prune live
        // genesis nodes.
        if (features.get(Flag::feature_l2_ethereum_compat) &&
            !co_await bcos::storage2::existsOne(*m_backend, seedMarkerKey()))
        {
            BOOST_THROW_EXCEPTION(bcos::tool::InvalidConfig{}
                                  << bcos::errinfo_comment(
                                         "MPT pruning startup guard: L2 genesis at block 0 "
                                         "carries no seeded pruning refcounts (no " +
                                         std::string{kPruneMetaTable} +
                                         std::string{kSeedMarkerRowKey} +
                                         " row) — the genesis was written by a binary predating "
                                         "refcount seeding; refusing to start with "
                                         "storage.mpt_prune_window enabled"));
        }
    }

    /// The pruning rows for @p blockNumber: metadata upserts AND the deletions of expired
    /// nodes, for the block's own WriteBatch. Pure computation plus batched reads against the
    /// committed backend; issues no writes itself.
    bcos::task::Task<PruneRowBatch> coPreparePruneRows(
        bcos::protocol::BlockNumber blockNumber, MPTDeltaLayer const& delta) override
    {
        PruneRowBatch out;
        // The watermark advances with EVERY block, delta or not: it is the startup guard's
        // record of the highest block whose pruning metadata is persisted.
        storage::Entry watermarkEntry;
        watermarkEntry.set(encodeWatermark(static_cast<uint64_t>(blockNumber)));
        out.rows.emplace_back(watermarkKey(), std::move(watermarkEntry));

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

        // The post-block refcount of every touched hash: the overlay the deletion re-check
        // below consults FIRST, so a node this very block revived (0→>0) reads as alive even
        // though the backend still shows its pre-block count 0.
        std::unordered_map<bcos::h256, PruneRefCount> postBlock;
        if (!movements.empty())
        {
            std::vector<bcos::h256> hashes;
            hashes.reserve(movements.size());
            for (auto const& hash : movements | std::views::keys)
            {
                hashes.push_back(hash);
            }
            auto const refEntries = co_await bcos::storage2::readSome(*m_backend,
                hashes | ::ranges::views::transform(
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
                    static_cast<uint64_t>(
                        std::max<int64_t>(0, static_cast<int64_t>(oldCount) + movement));
                bool const wasObsoleted = delta.obsoletedNodes.contains(hash) ||
                                          delta.intraBlockObsoleted.contains(hash);

                bool changed = (newCount != oldCount);
                if (newCount == 0 && wasObsoleted && !refCount.pendingDeleteAt)
                {
                    // >0→0, or the saturating 0→0 of a node that never passed a delta (only
                    // reachable from hand-built test deltas — genesis and pre-activation nodes
                    // are covered by seeding and the startup guard): schedule the deletion.
                    refCount.pendingDeleteAt =
                        static_cast<uint64_t>(blockNumber + m_pruneWindow);
                    changed = true;
                    storage::Entry queueEntry;
                    queueEntry.set(std::string{});
                    out.rows.emplace_back(
                        pruneQueueKey(*refCount.pendingDeleteAt, hash), std::move(queueEntry));
                }
                else if (newCount > 0 && refCount.pendingDeleteAt)
                {
                    // 0→>0: revived before its deletion ran — revoke the schedule. The queue row
                    // itself is cleaned lazily by the deletion consumption below.
                    refCount.pendingDeleteAt.reset();
                    changed = true;
                }
                refCount.count = newCount;
                postBlock.emplace(hash, refCount);
                if (!changed)
                {
                    continue;
                }
                storage::Entry refEntry;
                refEntry.set(encodeRefCount(refCount));
                out.rows.emplace_back(pruneRefKey(hash), std::move(refEntry));
            }
        }

        // Consume the expired delete queue: keys are (BE-u64 targetBlock ‖ hash) inside one
        // table, so the seek lands on the oldest deadline and iteration stops at the first row
        // beyond this block. Capped at m_deleteBatchSize per block — a backlog continues with
        // the next block's prepare.
        struct Candidate
        {
            bcos::executor_v1::StateKey queueKey;
            uint64_t targetBlock;
            bcos::h256 hash;
        };
        std::vector<Candidate> batch;
        auto const horizon = static_cast<uint64_t>(blockNumber);
        auto iterator = co_await bcos::storage2::range(*m_backend, bcos::storage2::RANGE_SEEK,
            bcos::executor_v1::StateKey{kPruneQueueTable, std::string_view{}});
        while (auto item = co_await iterator.next())
        {
            auto const& key = std::get<0>(*item);
            bcos::executor_v1::StateKeyView const keyView{key};
            if (keyView.m_table != kPruneQueueTable)
            {
                break;
            }
            if (!std::get_if<storage::Entry>(std::addressof(std::get<1>(*item))))
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
        if (!batch.empty())
        {
            // Re-check before deleting: a queue row is only a hint — the refcount row (post-this
            // -block, via postBlock) is the verdict. count == 0 AND pendingDeleteAt == this
            // row's targetBlock confirms the schedule was never revoked or re-armed (a re-armed
            // node carries a NEWER pendingDeleteAt, mismatching this stale row).
            std::vector<size_t> backendReadIndex(batch.size(), SIZE_MAX);
            std::vector<bcos::executor_v1::StateKey> backendReadKeys;
            for (size_t i = 0; i < batch.size(); ++i)
            {
                if (!postBlock.contains(batch[i].hash))
                {
                    backendReadIndex[i] = backendReadKeys.size();
                    backendReadKeys.push_back(pruneRefKey(batch[i].hash));
                }
            }
            auto const refEntries =
                co_await bcos::storage2::readSome(*m_backend, backendReadKeys);

            for (size_t i = 0; i < batch.size(); ++i)
            {
                std::optional<PruneRefCount> refCount;
                if (auto const post = postBlock.find(batch[i].hash); post != postBlock.end())
                {
                    refCount = post->second;
                }
                else if (auto const& entry = refEntries[backendReadIndex[i]]; entry)
                {
                    auto raw = entry->get();
                    refCount = decodeRefCount(bcos::bytesConstRef(
                        reinterpret_cast<bcos::byte const*>(raw.data()), raw.size()));
                }
                bool const confirmed = refCount && refCount->count == 0 &&
                                       refCount->pendingDeleteAt == batch[i].targetBlock &&
                                       // Unreachable under correct accounting (an emission this
                                       // block implies a positive post-block count), kept as a
                                       // belt-and-braces: never delete a node this block's own
                                       // flush is writing in the same WriteBatch — a leak
                                       // (retryable next block) beats a deleted live node.
                                       !delta.newNodes.contains(batch[i].hash);
                if (confirmed)
                {
                    out.deletions.push_back(bcos::ledger::mptNodeStateKey(batch[i].hash));
                    out.deletions.push_back(pruneRefKey(batch[i].hash));
                }
                out.deletions.push_back(std::move(batch[i].queueKey));
            }
        }
        co_return out;
    }

    /// After the block's WriteBatch: advance the in-memory watermark. Deletions already landed
    /// with the batch — there is nothing to hand off. The CommitObserver contract forbids
    /// throwing and blocking here.
    void onCommit(
        bcos::protocol::BlockNumber blockNumber, MPTDeltaLayer const& /*delta*/) override
    {
        auto current = m_watermark.load(std::memory_order_relaxed);
        while (current < blockNumber &&
               !m_watermark.compare_exchange_weak(
                   current, blockNumber, std::memory_order_relaxed))
        {
        }
    }

    /// Highest committed block number this pruner has seen (persisted watermark at init(),
    /// onCommit afterwards). −1 before either.
    bcos::protocol::BlockNumber watermark() const noexcept
    {
        return m_watermark.load(std::memory_order_relaxed);
    }

private:
    Backend* m_backend;
    int64_t m_pruneWindow;
    size_t m_deleteBatchSize;
    std::atomic<int64_t> m_watermark{-1};
};

}  // namespace bcos::ledger::mpt
