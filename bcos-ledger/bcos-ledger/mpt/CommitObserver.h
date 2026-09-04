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
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Task.h>
#include <utility>
#include <vector>

namespace bcos::ledger::mpt
{

/// One block's pruning rows, exchanged between the observer and the commit flow. `rows` are
/// upserts (refcount / delete-queue / watermark), `deletions` the keys to remove (expired
/// "/mpt/" node rows plus the consumed metadata rows). Both are keyed by executor_v1::StateKey
/// (not an h256/bytes node-storage pair) because the pruning metadata lives in ordinary
/// "/sys/mpt_prune_*" state tables and the node rows in "/mpt/" — all must merge into the
/// block's prewriteStorage alongside the flat state, so the commit flow applies them with one
/// storage2::writeSome + one storage2::removeSome and no MPT-specific code of its own, and the
/// deletions land in the SAME WriteBatch as the block data (no "metadata says deleted, node
/// row still there" — or the reverse — crash window).
struct PruneRowBatch
{
    std::vector<std::pair<bcos::executor_v1::StateKey, bcos::storage::Entry>> rows;
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
    /// storage layers merge, and returns the pruning metadata rows AND the deletions of expired
    /// nodes, both landing in the SAME WriteBatch as the block data — metadata, data and
    /// deletions can never diverge across a crash, and the deletion decision (made under the
    /// commit mutex, against the committed backend plus this block's own overlay) can never
    /// race a concurrent commit reviving the node. Pure computation plus batched reads against
    /// the committed backend — the caller owns applying the returned rows. The default returns
    /// an empty batch: observers without pruning metadata (e.g. NoopCommitObserver) ignore the
    /// hook.
    virtual bcos::task::Task<PruneRowBatch> coPreparePruneRows(
        bcos::protocol::BlockNumber /*blockNumber*/, MPTDeltaLayer const& /*delta*/)
    {
        co_return {};
    }

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
