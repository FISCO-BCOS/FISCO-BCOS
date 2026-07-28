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
};

/// Merge one commitTrie() result into @p delta: newNodes overwrite-insert, obsoleted hashes
/// merge. Taken by value (moved from): the result is consumed, buffers are moved not copied.
/// Duck-typed on the newNodes/obsoletedNodes members TrieMergeResult defines, so this pure-data
/// header does not depend on the trie headers.
template <typename NodeDelta>
void mergeNodeDelta(NodeDelta merged, MPTDeltaLayer& delta)
{
    for (auto& [hash, raw] : merged.newNodes)
    {
        delta.newNodes.insert_or_assign(hash, std::move(raw));
    }
    delta.obsoletedNodes.merge(std::move(merged.obsoletedNodes));
}

}  // namespace bcos::ledger::mpt
