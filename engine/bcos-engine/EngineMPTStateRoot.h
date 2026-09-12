/**
 * Copyright (C) 2026 FISCO BCOS.
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * @file EngineMPTStateRoot.h
 * @brief MPT state-root resolution for Engine-driven block production (Eth/Op services).
 */

#pragma once

#include <bcos-crypto/interfaces/crypto/Hash.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-ledger/mpt/CommitObserver.h>
#include <bcos-task/Task.h>
// Shared state-root derivations (xorStateRoot / buildMPTStateRootForView /
// publishPendingBlockHeaderForMPT) live in the transaction-scheduler package so the engine
// and the PBFT scheduler cannot drift on the parent-root rule or the XOR features argument.
#include <bcos-transaction-scheduler/BaselineSchedulerMPTHelpers.h>

#include <memory>
#include <optional>

namespace bcos::engine::engine_common
{

/// The result of resolveEngineBlockStateRoot: the header's state root plus, on the MPT branch,
/// the block's full node delta. The engine commit path stashes the delta in the payload
/// artifact so newPayload can hand it to the CommitObserver (MPT pruning) when the block
/// commits — the observer hooks live at the "MPT delta -> backend merge" layer, not in this
/// derivation.
struct EngineStateRootResolution
{
    h256 stateRoot;
    std::optional<ledger::mpt::MPTDeltaLayer> mptDelta;
};

/// Wrap a resolved MPT delta for payload-artifact storage: the entry keeps it until the
/// durable write succeeds, and the commit path's retry re-reads it — shared ownership avoids
/// copying the node map per attempt.
inline std::shared_ptr<const ledger::mpt::MPTDeltaLayer> shareMptDelta(
    std::optional<ledger::mpt::MPTDeltaLayer> mptDelta)
{
    if (!mptDelta)
    {
        return nullptr;
    }
    return std::make_shared<const ledger::mpt::MPTDeltaLayer>(std::move(*mptDelta));
}

/// Resolve the block header's state root: MPT when shouldBuildMPT, otherwise the legacy XOR
/// fold. Both derivations are shared with the PBFT scheduler (BaselineSchedulerMPTHelpers.h);
/// the XOR branch passes ledgerConfig.features() so the v3.17 hash fix applies identically on
/// both paths.
///
/// @param commitObserver the engine service's pruning observer: its needsRefCountDeltas() is
/// the single decision point for the refCountDeltas tally, so the build cannot drift from the
/// commit hook — with a counting observer an untallied delta would trip the pruner's fail-loud
/// empty-refCountDeltas check on the first pruned block. NoopCommitObserver (pruning off)
/// skips the tally, and the returned mptDelta is then still carried to commit, where the
/// Noop observer ignores it.
template <class ViewType>
task::Task<EngineStateRootResolution> resolveEngineBlockStateRoot(ViewType& view,
    protocol::BlockHeader& blockHeader, ledger::LedgerConfig const& ledgerConfig,
    crypto::Hash const& hashImpl, protocol::BlockFactory& blockFactory,
    ledger::mpt::CommitObserver const& commitObserver)
{
    auto const blockNumber = blockHeader.number();
    if (scheduler_v1::shouldBuildMPT(ledgerConfig.features(), blockNumber))
    {
        scheduler_v1::rejectRawAddressWithMPT(ledgerConfig.features(), blockNumber);
        auto mptDelta = co_await scheduler_v1::buildMPTStateRootForView(view, blockHeader,
            ledgerConfig, blockFactory, commitObserver.needsRefCountDeltas());
        auto const stateRoot = mptDelta.stateRoot;
        blockHeader.setStateRoot(stateRoot);
        co_await scheduler_v1::publishPendingBlockHeaderForMPT(view, blockHeader);
        co_return EngineStateRootResolution{stateRoot, std::move(mptDelta)};
    }
    auto stateRoot = co_await scheduler_v1::xorStateRoot(
        view, blockHeader.version(), hashImpl, ledgerConfig.features());
    blockHeader.setStateRoot(stateRoot);
    co_return EngineStateRootResolution{stateRoot, std::nullopt};
}

}  // namespace bcos::engine::engine_common
