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
 * @file MPTDeltaLayer.h
 * @brief One block's MPT node delta — the contract between MPTBuilder and the commit
 *        flow / MultiLayerStorage (spec §5.7)
 */
#pragma once

#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace bcos::ledger::mpt
{

/// One block's MPT build product: the new state root plus the node delta the commit path must
/// persist alongside the flat state. Produced by MPTBuilder::buildAndCollect; consumed by the
/// commit flow (PR-14b) and passed to CommitObserver after the block's WriteBatch lands.
///
/// newNodes and obsoletedNodes are DISJOINT (buildAndCollect end-subtracts, mirroring mergeTrie):
/// a consumer never has to order a write against a delete of the same node. The subtracted hashes
/// are not lost — they move to intraBlockObsoleted so the pruning spec keeps full information.
struct MPTDeltaLayer
{
    /// The post-block MPT state root.
    bcos::h256 stateRoot;
    /// Every newly produced node (hash → raw RLP), aggregated from every commitTrie() result of
    /// this block. buildAndCollect's end-of-build flush writes these as ordinary "/mpt/:<hash>"
    /// state rows into the block's mutable layer, so commit persists them in the SAME WriteBatch
    /// as the flat state (single db->Write, spec §5.6); this map stays the CommitObserver's
    /// payload. The flush COPIES each node's RLP into its row's Entry (it takes the map by
    /// const reference and never moves out of it), so newNodes is still complete when
    /// CommitObserver::onCommit receives the delta after the WriteBatch lands.
    std::unordered_map<bcos::h256, bcos::bytes> newNodes;
    /// Nodes that the tries rebuilt by THIS block no longer reference, disjoint from newNodes.
    ///
    /// CANDIDATES, not a delete list. Each hash is judged inside one trie's rebuild (mergeTrie
    /// phase 4: resolved minus re-emitted), which cannot see the rest of the state. Nodes are
    /// content-addressed and therefore shared: two accounts whose subtries encode identically —
    /// same slot, same value — resolve to the same hash and the same stored node. So a hash
    /// listed here may still be referenced by an account this block never touched, and deleting
    /// it would make that account's storage permanently unreadable.
    ///
    /// Contract (spec §4.8): an input ledger for the future pathdb pruning spec ONLY. The commit
    /// flow does not read it and issues no deletes from it. Whatever consumes it must establish
    /// unreachability itself (reference counting or an equivalent), never trust membership here
    /// as permission to delete.
    std::unordered_set<bcos::h256> obsoletedNodes;
    /// Nodes produced AND obsoleted within this same block (identical RLP encodings hash
    /// identically across the block's per-account trie builds, so one build's new node can
    /// byte-match another's obsoleted one). They are in newNodes — written to disk — so they are
    /// deliberately kept OUT of obsoletedNodes (a consumer must not delete a node it also writes
    /// in one batch). Parked here rather than dropped so the pruning spec can decide their fate
    /// with full information; erasing them would orphan them permanently.
    std::unordered_set<bcos::h256> intraBlockObsoleted;
    /// Per-hash NET reference movement of this block, tallied by mergeNodeDelta: +1 for every
    /// commitTrie() result that EMITTED the hash, −1 for every result that obsoleted it, −1 for
    /// every result that RE-EMITTED it byte-identically from the prior version
    /// (TrieMergeResult::reemittedNodes — such a hash sits in newNodes, but the same trie
    /// referenced it before and after, so its reference count must not move). The pruning
    /// spec's reference counting needs this because newNodes cannot carry it: newNodes
    /// is a deduplicated map, but two accounts whose builds produce the byte-identical node in
    /// one block create TWO references to it (content-addressed sharing, see obsoletedNodes
    /// above) and later rebuilds will obsolete it once per referencing trie. Counting each
    /// result's emission — not each surviving map entry — keeps "all-history adds − removes =
    /// live references" exact across same-block cross-trie sharing; counting the deduplicated
    /// sets would under-count the creations and delete the node while the second trie still
    /// references it. Without the re-emit correction the same tally would drift UP by one on
    /// every no-net-change rebuild of a node (mergeTrie phase 4 deliberately keeps re-emitted
    /// nodes out of obsoletedNodes, so the balancing −1 would never arrive) and the node would
    /// leak on disk forever.
    ///
    /// Unlike the three containers above this map is NOT disjointness-adjusted by
    /// buildAndCollect's end-subtraction — it already nets every emission against every
    /// obsoletion, so an intraBlockObsoleted hash reads as its true net movement (typically 0:
    /// one reference lost, one created).
    std::unordered_map<bcos::h256, int64_t> refCountDeltas;
};

/// Merge one commitTrie() result into @p delta: newNodes overwrite-insert, obsoleted hashes
/// merge, and the reference movement is tallied per hash. Taken by value (moved from): the
/// result is consumed, buffers are moved not copied. Duck-typed on the newNodes/obsoletedNodes
/// /reemittedNodes members TrieMergeResult defines, so this pure-data header does not depend on
/// the trie headers.
/// @param trackRefCounts false skips the refCountDeltas tally (the map is left untouched, not
/// zeroed) for callers whose CommitObserver does not count references — see
/// CommitObserver::needsRefCountDeltas; newNodes/obsoletedNodes merge exactly as before.
template <typename NodeDelta>
void mergeNodeDelta(NodeDelta merged, MPTDeltaLayer& delta, bool trackRefCounts = true)
{
    for (auto& [hash, raw] : merged.newNodes)
    {
        // Every emission is one reference CREATION, even when the map already holds the hash:
        // a second trie building the byte-identical node in this same block references it too.
        if (trackRefCounts)
        {
            ++delta.refCountDeltas[hash];
        }
        delta.newNodes.insert_or_assign(hash, std::move(raw));
    }
    if (trackRefCounts)
    {
        for (auto const& hash : merged.obsoletedNodes)
        {
            --delta.refCountDeltas[hash];
        }
        for (auto const& hash : merged.reemittedNodes)
        {
            // Resolved from the prior version and re-emitted byte-identically: the SAME trie
            // keeps referencing it, so cancel the +1 the emission loop tallied — net movement is
            // zero. (A hash is in exactly one of obsoletedNodes / reemittedNodes per result, so
            // the two corrections never double-count.)
            --delta.refCountDeltas[hash];
        }
    }
    delta.obsoletedNodes.merge(std::move(merged.obsoletedNodes));
}

}  // namespace bcos::ledger::mpt
