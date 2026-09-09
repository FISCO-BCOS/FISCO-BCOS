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
 * @file PathDiff.h
 * @brief One block's path-addressed node delta — the contract between MPTBuilder and the commit
 *        flow (pathdb spec §9, appendix A.7/A.8)
 */
#pragma once

#include "PathKey.h"
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace bcos::ledger::mpt
{

/// One block's MPT build product: the new state root plus the node rows the commit path must
/// write, delete and (later) archive.
///
/// upserts and deletes are DISJOINT by construction — a position the rebuild re-emits is written,
/// not deleted, so a consumer never has to order a write against a delete of the same row. There
/// is no obsoletion ledger and no reference counting: under path addressing "this position now
/// holds different bytes" IS the whole statement, and a position no node occupies any more is a
/// local, provable consequence of the rebuild rather than a global reachability question.
struct PathDiff
{
    /// The post-block MPT state root.
    bcos::h256 stateRoot;

    /// Position -> the new RLP that must live there. Written as ordinary state rows into the
    /// block's mutable layer, so the existing single mergeBackStorage persists them in the SAME
    /// WriteBatch as the flat state.
    std::map<PathKey, bcos::bytes> upserts;

    /// Positions no node occupies in the new version (spec A.6): a branch collapse absorbed the
    /// node into its parent, an extension merged with its child, the node shrank below the
    /// 32-byte inline threshold and now lives inside its parent's encoding, or its whole trie was
    /// dropped. Deleted as ordinary state rows in the same batch.
    ///
    /// Deleting one position too FEW leaves an unreachable row (waste). Deleting one too MANY
    /// punches a hole in a live trie and stops the chain, so every delete here traces to a row
    /// this block actually read.
    std::set<PathKey> deletes;

    /// Accounts whose ENTIRE storage trie went away this block (spec A.6 source 4). The
    /// corresponding rows are already enumerated into `deletes`; this records WHOSE they were,
    /// which the row keys alone would force a consumer to re-derive.
    std::vector<bcos::h256> droppedStorageTries;

    /// What each touched position held BEFORE this block — nullopt meaning "nothing was there".
    /// Covers every position in upserts and in deletes (spec A.7): a newly occupied position must
    /// record its absence too, or a reader walking backwards would conclude the node had always
    /// been there. Consumed by the trie-node history the pruning spec builds; nothing in this PR
    /// reads it.
    std::map<PathKey, std::optional<bcos::bytes>> preimages;
};

/// Merge one commitTrie() result into @p diff. Taken by value (moved from): the result is
/// consumed, buffers are moved not copied. Duck-typed on the upserts/deletes/preimages members
/// PathMergeResult defines, so this pure-data header does not depend on the trie headers.
///
/// No cross-trie de-duplication is needed or possible: every trie has its own TrieScope, so two
/// tries cannot name the same row even when they contain byte-identical nodes. (That is the same
/// property, seen from the other side, that made the old hash-keyed delta need an intra-block
/// subtraction pass.)
template <typename NodeDelta>
void mergePathDelta(NodeDelta merged, PathDiff& diff)
{
    for (auto& [key, raw] : merged.upserts)
    {
        diff.upserts.insert_or_assign(key, std::move(raw));
    }
    diff.deletes.merge(std::move(merged.deletes));
    for (auto& [key, prior] : merged.preimages)
    {
        diff.preimages.insert_or_assign(key, std::move(prior));
    }
}

}  // namespace bcos::ledger::mpt
