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
 * @file CommitObserver.h
 * @brief Post-commit hook over the block's MPTDeltaLayer — the pathdb pruning seam (spec §4.8)
 */
#pragma once

#include "MPTDeltaLayer.h"
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Task.h>
#include <vector>

namespace bcos::ledger::mpt
{

/// One block's pruning rows, exchanged between the observer and the commit flow: `deletions`
/// are the keys to remove (expired "/mpt/" node rows). Keyed by executor_v1::StateKey (not an
/// h256/bytes node-storage pair) because the node rows live in the ordinary "/mpt/" state
/// table and must merge into the block's prewriteStorage alongside the flat state, so the
/// commit flow applies them with one storage2::removeSome and no MPT-specific code of its own,
/// and the deletions land in the SAME WriteBatch as the block data (no "node row deleted,
/// block data lost" — or the reverse — crash window). Pruning keeps no metadata on disk — all
/// of its state is in memory (MPTPruner) — so there are no upsert rows to carry.
struct PruneRowBatch
{
    std::vector<bcos::executor_v1::StateKey> deletions;
};

/// The seam for the pathdb pruning spec (§4.8): pruning subscribes to every block's node delta
/// without the commit flow knowing pruning exists. MPTPruner is the live implementation.
class CommitObserver
{
public:
    CommitObserver() = default;
    virtual ~CommitObserver() = default;

    /// Timing contract (spec §5.6): the commit flow calls this AFTER the block's WriteBatch
    /// has landed on disk and BEFORE lastCommittedBlockNumber advances, so the delta the
    /// observer sees is exactly the persisted state. Runs on the commit path — implementations
    /// must not throw and must not block on slow work.
    virtual void onCommit(bcos::protocol::BlockNumber blockNumber, MPTDeltaLayer const& delta) = 0;

    /// Pre-commit counterpart of onCommit: called inside the commit coroutine BEFORE the block's
    /// storage layers merge, and returns the deletions of expired node rows, landing in the
    /// SAME WriteBatch as the block data — data and deletions can never diverge across a crash,
    /// and the deletion decision (made under the commit mutex, against the committed backend
    /// plus this block's own overlay) can never race a concurrent commit reviving the node.
    /// Pure computation plus batched reads against the committed backend — the caller owns
    /// applying the returned keys. The default returns an empty batch: observers without
    /// pruning (e.g. NoopCommitObserver) ignore the hook.
    virtual bcos::task::Task<PruneRowBatch> coPreparePruneRows(
        bcos::protocol::BlockNumber /*blockNumber*/, MPTDeltaLayer const& /*delta*/)
    {
        co_return {};
    }

    /// Whether this observer consumes MPTDeltaLayer::refCountDeltas (the pruning reference
    /// tally). The execute path consults this at build time: when false, mergeNodeDelta skips
    /// the per-hash tally entirely and the delta's refCountDeltas arrives empty — so a
    /// non-counting observer must also leave coPreparePruneRows at the default (the empty batch),
    /// because the pruner's set-based fallback reading of an untallied delta would over-count.
    /// NoopCommitObserver keeps the default; MPTPruner overrides it to true.
    virtual bool needsRefCountDeltas() const noexcept { return false; }

protected:
    // Protected, not public: derived observers keep their own defaults, but outside code cannot
    // slice a CommitObserver through a base reference (Core Guidelines C.67).
    CommitObserver(CommitObserver const&) = default;
    CommitObserver(CommitObserver&&) = default;
    CommitObserver& operator=(CommitObserver const&) = default;
    CommitObserver& operator=(CommitObserver&&) = default;
};

/// The default observer until the pruning spec lands: receives and ignores every delta.
class NoopCommitObserver : public CommitObserver
{
public:
    void onCommit(
        bcos::protocol::BlockNumber /*blockNumber*/, MPTDeltaLayer const& /*delta*/) override
    {}
};

}  // namespace bcos::ledger::mpt
