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
#include <vector>

namespace bcos::ledger::mpt
{

/// One block's MPT build product: the new state root plus the node delta the commit path must
/// persist alongside the flat state. Produced by MPTBuilder::buildAndCollect (which returns it
/// under its original name, MPTBuildOutput); consumed by the commit flow (PR-14b) and passed to
/// CommitObserver after the block's WriteBatch lands.
///
/// The two node sets are DISJOINT (buildAndCollect end-subtracts, mirroring mergeTrie): a
/// consumer never has to order a write against a delete of the same node.
struct MPTDeltaLayer
{
    /// The post-block MPT state root.
    bcos::h256 stateRoot;
    /// Every newly produced node (hash → raw RLP), same contract as
    /// HashBuilder::drainNewNodes(). The commit flow writes these under /mpt/<hash> keys in the
    /// SAME WriteBatch as the flat state (single db->Write, spec §5.6).
    std::unordered_map<bcos::h256, bcos::bytes> newNodes;
    /// Nodes this block made unreachable, same contract as HashBuilder::drainObsoletedNodes().
    /// Contract (spec §4.8): an input ledger for the future pathdb pruning spec ONLY — the
    /// commit flow does not read it and issues no deletes from it.
    std::unordered_set<bcos::h256> obsoletedNodes;
    /// Accounts whose preheat manifest record was consumed by this build (first-touch path 2b,
    /// spec §5.11). The commit flow deletes each record via PreheatManifest::remove in the same
    /// WriteBatch — a consumed root must not seed a second first-touch.
    std::vector<bcos::Address> preheatManifestsToDelete;
};

/// Drain a builder's produced node delta into @p delta: newNodes overwrite-insert, obsoleted
/// hashes merge. Duck-typed on the drainNewNodes()/drainObsoletedNodes() interface HashBuilder
/// defines, so this pure-data header does not depend on the builder template.
template <typename Builder>
void absorbNodeDelta(Builder& builder, MPTDeltaLayer& delta)
{
    for (auto& [hash, raw] : builder.drainNewNodes())
    {
        delta.newNodes.insert_or_assign(hash, std::move(raw));
    }
    delta.obsoletedNodes.merge(builder.drainObsoletedNodes());
}

}  // namespace bcos::ledger::mpt
