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
#include <bcos-task/Task.h>
// Shared state-root derivations (xorStateRoot / buildMPTStateRootForView /
// publishPendingBlockHeaderForMPT) live in the transaction-scheduler package so the engine
// and the PBFT scheduler cannot drift on the parent-root rule or the XOR features argument.
#include <bcos-transaction-scheduler/BaselineSchedulerMPTHelpers.h>

namespace bcos::engine::engine_common
{

/// Resolve the block header's state root: MPT when shouldBuildMPT, otherwise the legacy XOR
/// fold. Both derivations are shared with the PBFT scheduler (BaselineSchedulerMPTHelpers.h);
/// the XOR branch passes ledgerConfig.features() so the v3.17 hash fix applies identically on
/// both paths.
template <class ViewType>
task::Task<h256> resolveEngineBlockStateRoot(ViewType& view, protocol::BlockHeader& blockHeader,
    ledger::LedgerConfig const& ledgerConfig, crypto::Hash const& hashImpl,
    protocol::BlockFactory& blockFactory)
{
    auto const blockNumber = blockHeader.number();
    if (scheduler_v1::shouldBuildMPT(ledgerConfig.features(), blockNumber))
    {
        scheduler_v1::rejectRawAddressWithMPT(ledgerConfig.features(), blockNumber);
        auto mptDelta = co_await scheduler_v1::buildMPTStateRootForView(
            view, blockHeader, ledgerConfig, blockFactory);
        blockHeader.setStateRoot(mptDelta.stateRoot);
        co_await scheduler_v1::publishPendingBlockHeaderForMPT(view, blockHeader);
        co_return mptDelta.stateRoot;
    }
    auto stateRoot = co_await scheduler_v1::xorStateRoot(
        view, blockHeader.version(), hashImpl, ledgerConfig.features());
    blockHeader.setStateRoot(stateRoot);
    co_return stateRoot;
}

}  // namespace bcos::engine::engine_common
