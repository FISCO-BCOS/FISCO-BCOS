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
 * @file BaselineSchedulerMPTHelpers.h
 * @brief MPT helpers shared by the PBFT baseline scheduler and the engine services.
 *        The feature gates and the XOR fold themselves live in bcos-ledger
 *        (mpt/MPTFeatureGates.h, mpt/StateRoots.h — namespace bcos::ledger::mpt) so every
 *        state-root producer applies the SAME transition rule; this header aliases the
 *        gates into scheduler_v1, forwards the XOR fold, and defines the shared
 *        build/publish/prune helpers (buildMPTStateRootForView,
 *        publishPendingBlockHeaderForMPT, prepareMPTPruneRows).
 */
#pragma once

#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/protocol/BlockFactory.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include <bcos-ledger/mpt/CommitObserver.h>
#include <bcos-ledger/mpt/MPTDeltaLayer.h>
#include <bcos-ledger/mpt/MPTFeatureGates.h>
#include <bcos-ledger/mpt/StateRoots.h>
#include <bcos-task/Task.h>
#include <boost/lexical_cast.hpp>

namespace bcos::scheduler_v1
{
using ledger::mpt::InvalidMPTFlagMatrix;
using ledger::mpt::rejectRawAddressWithMPT;
using ledger::mpt::shouldBuildMPT;
using ledger::mpt::validateMPTFlagMatrix;

/// XOR fold over flat storage — the legacy (non-MPT) state-root path, shared by the PBFT
/// scheduler and the engine service. The implementation lives in bcos-ledger
/// (ledger::mpt::computeLegacyStateRoot, StateRoots.h) so every producer folds the same way
/// (BOTH callers must pass @p features to Entry::hash: the v3.17 bugfix flag
/// bugfix_statestorage_hash_v3_17 changes the digest); this alias keeps the scheduler_v1
/// spelling the engine services use.
template <class StorageType>
task::Task<h256> xorStateRoot(StorageType& storage, uint32_t blockVersion,
    crypto::Hash const& hashImpl, ledger::Features const& features)
{
    co_return co_await ledger::mpt::computeLegacyStateRoot(
        storage, blockVersion, hashImpl, features);
}

/// Backwards-compatible name for xorStateRoot, kept for the FIB-99/FIB-105 state-root tests.
template <class StorageType>
task::Task<h256> calculateStateRoot(StorageType& storage, uint32_t blockVersion,
    crypto::Hash const& hashImpl, ledger::Features const& features)
{
    co_return co_await xorStateRoot(storage, blockVersion, hashImpl, features);
}

/// Build an Ethereum MPT state root over @p view. The parent-root rule is single-sourced in
/// bcos-ledger (ledger::mpt::parentStateRootFor, StateRoots.h): the parent's committed state
/// root is read only when the parent itself built an MPT, so an activation-boundary parent
/// (XOR root) starts the build from the empty trie.
///
/// @param trackRefCounts  forwarded to buildAndCollect: false skips the per-hash
///                        refCountDeltas tally for callers whose commit path never reads it.
///                        Pass the commit observer's needsRefCountDeltas() (the PBFT scheduler
///                        and the engine services both do). Defaults to false so a producer
///                        that forgets to wire its observer through fails LOUD — the pruner's
///                        empty-refCountDeltas check (MPTPruner::coPreparePruneRows) throws on
///                        the first pruned block — instead of silently tallying with no
///                        consumer.
template <class ViewType>
task::Task<ledger::mpt::MPTDeltaLayer> buildMPTStateRootForView(ViewType& view,
    protocol::BlockHeader const& blockHeader, ledger::LedgerConfig const& ledgerConfig,
    protocol::BlockFactory& blockFactory, bool trackRefCounts = false)
{
    h256 parentStateRoot = co_await ledger::mpt::parentStateRootFor(
        view, ledgerConfig.features(), blockHeader.number(), blockFactory);
    // Node reads resolve through the full view (parent nodes live in the pending layers /
    // backend); node writes land in this block's own mutable layer (ViewNodeStorage.h).
    ledger::mpt::ViewNodeStorage<ViewType> nodeStorage(view);
    bool const l2Mode =
        ledgerConfig.features().get(ledger::Features::Flag::feature_l2_ethereum_compat);
    co_return co_await ledger::mpt::buildAndCollect(
        nodeStorage, parentStateRoot, view, l2Mode, trackRefCounts);
}

/// Publish the header under SYS_NUMBER_2_BLOCK_HEADER so the next block's MPT build can read
/// the parent's committed state root through the view.
///
/// NOT byte-equivalent to a fully signed header when called at execute time: the engine calls
/// it before receiptsRoot / txsRoot / gasUsed are set and before the hash is computed, so the
/// row carries defaults for those fields. Two facts make that safe, and any new reader must
/// re-check them: (1) the only reader before the commit overwrites the row is the next
/// block's parent-stateRoot lookup, which needs only stateRoot; (2) the commit's prewrite
/// layer merges after the block layer, so the signed header wins in the backend.
template <class ViewType>
task::Task<void> publishPendingBlockHeaderForMPT(
    ViewType& view, protocol::BlockHeader const& header)
{
    if (header.number() == 0)
    {
        co_return;
    }
    auto blockNumberStr = boost::lexical_cast<std::string>(header.number());
    bytes headerBuffer;
    header.encode(headerBuffer);
    storage::Entry headerEntry;
    headerEntry.set(std::move(headerBuffer));
    co_await storage2::writeOne(view,
        executor_v1::StateKey{ledger::SYS_NUMBER_2_BLOCK_HEADER, blockNumberStr},
        std::move(headerEntry));
}

/// The pre-commit pruning hook, shared by every producer that lands an MPT delta (the PBFT
/// BaselineScheduler::coCommitBlock and the engine services' newPayload commit): the observer
/// turns the block's delta into the deletion keys of expired "/mpt/" node rows, applied to
/// @p prewriteStorage so the deletions land in the SAME WriteBatch as the block data (CommitObserver.h
/// explains the crash-atomicity contract). The NoopCommitObserver default returns an empty
/// batch, so a node without pruning configured pays nothing.
///
/// SERIALIZATION CONTRACT: MPTPruner stages the block's counting work on a single shared
/// overlay between this call and the matching CommitObserver::onCommit, so the caller must
/// hold its commit mutex across [prepareMPTPruneRows -> merge -> onCommit] and thereby
/// serialize the triple against every other commit (BaselineScheduler::m_commitMutex, the
/// engine services' m_commitMutex). A commit that fails before onCommit simply re-runs this
/// helper on retry — the staged overlay is discarded and re-derived (idempotent).
template <class MutableStorageType>
task::Task<void> prepareMPTPruneRows(ledger::mpt::CommitObserver& commitObserver,
    protocol::BlockNumber blockNumber, ledger::mpt::MPTDeltaLayer const& mptDelta,
    MutableStorageType& prewriteStorage)
{
    auto pruneRows = co_await commitObserver.coPreparePruneRows(blockNumber, mptDelta);
    if (!pruneRows.deletions.empty())
    {
        // The mutable layer is LOGICAL_DELETION: removeSome writes tombstones that the
        // merge turns into physical deletes in the backend's WriteBatch (and removals
        // in the cache fan-out).
        co_await storage2::removeSome(prewriteStorage, std::move(pruneRows.deletions));
    }
}

}  // namespace bcos::scheduler_v1
