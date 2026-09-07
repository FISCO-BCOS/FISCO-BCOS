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
 * @file StateRoots.h
 * @brief Block state-root computation, shared by every state-root producer
 *        (BaselineScheduler, EthereumBlockVerifier, EngineServiceImpl):
 *        - computeMptStateRoot: the real Ethereum world-state MPT root (moved from
 *          EthereumBlockVerifier::computeMptStateRoot);
 *        - computeLegacyStateRoot: the legacy XOR fold for non-MPT chains (moved from
 *          scheduler_v1::calculateStateRoot in BaselineScheduler.h).
 */
#pragma once

#include "MPTBuilder.h"
#include "ViewNodeStorage.h"
#include <bcos-crypto/interfaces/crypto/Hash.h>
#include <bcos-framework/ledger/Features.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Task.h>
#include <bcos-task/TBBWait.h>
#include <bcos-utilities/Common.h>
#include <oneapi/tbb/parallel_pipeline.h>
#include <oneapi/tbb/task_arena.h>
#include <optional>

namespace bcos::ledger::mpt
{

/// MPT state root over the executed view's Ethereum world state, built incrementally
/// from the parent block's state root. Accounts and their storage sub-tries enter the
/// trie; ledger metadata (SYS_* rows) never does.
///
/// NOTE: the build is INCREMENTAL — it needs the parent block's trie nodes resolvable
/// through the executed view (persisted by the previous block's commit), so blocks must be
/// built/verified strictly in order from a known state root. It also WRITES the new trie
/// nodes into the view's top mutable layer, so it must run EXACTLY ONCE per block.
template <class ViewType>
task::Task<crypto::HashType> computeMptStateRoot(ViewType& view,
    crypto::HashType const& parentStateRoot, ledger::LedgerConfig const& ledgerConfig)
{
    ViewNodeStorage<ViewType> nodeStorage(view);
    bool const l2Mode =
        ledgerConfig.features().get(ledger::Features::Flag::feature_l2_ethereum_compat);
    auto delta = co_await buildAndCollect(nodeStorage, parentStateRoot, view, l2Mode);
    co_return delta.stateRoot;
}

/// The legacy state root: an XOR fold over the per-entry hashes of the storage delta.
/// NOT collision-resistant — kept for chains whose feature flags select the legacy scheme
/// (shouldBuildMPT == false); MPT chains must use computeMptStateRoot.
task::Task<h256> computeLegacyStateRoot(auto& storage, uint32_t blockVersion,
    crypto::Hash const& hashImpl, ledger::Features const& features)
{
    auto range = co_await storage2::range(storage);
    storage::Entry deletedEntry;
    deletedEntry.setStatus(storage::Entry::DELETED);

    // Wrap once outside the parallel pipeline so the optional copy is paid only once,
    // not per entry.
    const std::optional<ledger::Features> featuresOpt(features);

    h256 totalHash;
    using KeyValueType = task::AwaitableReturnType<decltype(range.next())>;
    tbb::parallel_pipeline(tbb::this_task_arena::max_concurrency(),
        tbb::make_filter<void, KeyValueType>(tbb::filter_mode::serial_in_order,
            [&](tbb::flow_control& control) -> KeyValueType {
                if (auto keyValue = task::tbb::syncWait(range.next()))
                {
                    return keyValue;
                }
                control.stop();
                return {};
            }) &
            tbb::make_filter<KeyValueType, h256>(tbb::filter_mode::parallel,
                [&](KeyValueType keyValue) -> h256 {
                    auto& [key, value] = *keyValue;
                    executor_v1::StateKeyView view(key);
                    auto [tableName, keyName] = view.get();

                    const storage::Entry* entry = nullptr;
                    if (entry = std::get_if<storage::Entry>(std::addressof(value)); !entry)
                    {
                        entry = std::addressof(deletedEntry);
                    }
                    return entry->hash(tableName, keyName, hashImpl, blockVersion, featuresOpt);
                }) &
            tbb::make_filter<h256, void>(
                tbb::filter_mode::serial_out_of_order, [&](h256 hash) { totalHash ^= hash; }));
    co_return totalHash;
}

}  // namespace bcos::ledger::mpt
