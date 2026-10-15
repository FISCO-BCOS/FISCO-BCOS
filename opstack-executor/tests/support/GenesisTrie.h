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
 * @file GenesisTrie.h
 * @brief The genesis/parent-trie seeding the OP executor suites share: the delegate
 *        OpScheduler's incremental MPT build resolves the parent header's stateRoot
 *        against persisted "/mpt/" nodes, so a test that drives block execution over a
 *        committed pre-state must build + persist that state's trie first.
 */
#pragma once

#include <bcos-ledger/mpt/Constants.h>      // emptyRootHash
#include <bcos-ledger/mpt/HashBuilder.h>    // flushTrieNodes
#include <bcos-ledger/mpt/StateRoots.h>     // computeMptStateDelta
#include <bcos-ledger/mpt/ViewNodeStorage.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Wait.h>
#include <variant>

namespace opstack_test
{
/// Copy every flat row visible through @p from into @p to's top mutable layer. The
/// incremental MPT build scans the top mutable layer ONLY (buildAndCollect), so a
/// backend-merged seed is invisible to it — this re-materializes the committed state as
/// the genesis build's delta.
template <class From, class To>
void copyFlatRows(From& from, To& to)
{
    auto it = bcos::task::syncWait(bcos::storage2::range(from));
    while (auto kv = bcos::task::syncWait(it.next()))
    {
        auto const& [k, v] = *kv;
        if (auto const* entry = std::get_if<bcos::storage::Entry>(std::addressof(v)))
            bcos::task::syncWait(bcos::storage2::writeOne(to, k, *entry));
    }
}

/// Build the MPT over the committed state (parent = empty root) and persist every node
/// as "/mpt/" rows — the test-local mirror of Ledger::buildGenesisBlock's Ethereum-lane
/// genesis import. Returns the root to stamp on the parent header. (The pre-cutover name
/// was computeAndPersistGenesisTrie; the parent framing is the same call.)
template <class MLS>
bcos::h256 computeAndPersistParentTrie(MLS& mls)
{
    auto readView = mls.fork();  // read-through to the committed backend (never merged)
    auto buildView = mls.fork();
    buildView.newMutable();
    copyFlatRows(readView, buildView);
    bcos::ledger::LedgerConfig ledgerConfig;
    ledgerConfig.setExecutorVersion(bcos::ledger::OPSTACK_EXECUTOR_VERSION);
    auto delta = bcos::task::syncWait(bcos::ledger::mpt::computeMptStateDelta(
        buildView, bcos::ledger::mpt::emptyRootHash(), ledgerConfig));
    auto persistView = mls.fork();
    persistView.newMutable();
    bcos::ledger::mpt::ViewNodeStorage<typename MLS::ViewType> nodeStorage(persistView);
    bcos::task::syncWait(bcos::ledger::mpt::flushTrieNodes(nodeStorage, delta.newNodes));
    bcos::task::syncWait(mls.mergeView(std::move(persistView)));
    return delta.stateRoot;
}

}  // namespace opstack_test
