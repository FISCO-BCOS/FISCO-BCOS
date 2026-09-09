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
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/LedgerMethods.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-ledger/mpt/MPTBuilder.h>
#include <bcos-ledger/mpt/MPTDeltaLayer.h>
#include <bcos-task/Task.h>
#include <bcos-transaction-scheduler/BaselineSchedulerMPTHelpers.h>
#include <bcos-transaction-scheduler/MPTNodeStorage.h>
#include <boost/lexical_cast.hpp>

namespace bcos::engine::engine_common
{

/// XOR fold over flat storage — legacy path when shouldBuildMPT is false.
template <class ViewType>
task::Task<h256> xorStateRoot(ViewType& view, uint32_t blockVersion, crypto::Hash const& hashImpl)
{
    auto range = co_await storage2::range(view);
    h256 totalHash;
    while (auto keyValue = co_await range.next())
    {
        auto& [key, value] = *keyValue;
        executor_v1::StateKeyView viewKey(key);
        auto [tableName, keyName] = viewKey.get();

        storage::Entry entry;
        if (auto* e = std::get_if<storage::Entry>(std::addressof(value)))
        {
            entry = *e;
        }
        else
        {
            entry.setStatus(storage::Entry::DELETED);
        }
        totalHash ^= entry.hash(tableName, keyName, hashImpl, blockVersion);
    }
    co_return totalHash;
}

/// Build an Ethereum MPT state root over @p view, mirroring BaselineScheduler::buildMPTStateRoot.
template <class ViewType>
task::Task<ledger::mpt::MPTDeltaLayer> buildMPTStateRootForView(ViewType& view,
    protocol::BlockHeader const& blockHeader, ledger::LedgerConfig const& ledgerConfig,
    protocol::BlockFactory& blockFactory)
{
    auto const blockNumber = blockHeader.number();
    h256 parentStateRoot = ledger::mpt::emptyRootHash();
    if (blockNumber > 0 && scheduler_v1::shouldBuildMPT(ledgerConfig.features(), blockNumber - 1))
    {
        auto parentBlock =
            co_await ledger::getBlockData(view, blockNumber - 1, ledger::HEADER, blockFactory);
        parentStateRoot = parentBlock->blockHeader()->stateRoot();
    }

    scheduler_v1::ViewNodeStorage<ViewType> nodeStorage(view);
    bool const l2Mode =
        ledgerConfig.features().get(ledger::Features::Flag::feature_l2_ethereum_compat);
    co_return co_await ledger::mpt::buildAndCollect(nodeStorage, parentStateRoot, view, l2Mode);
}

/// Publish the executed header under SYS_NUMBER_2_BLOCK_HEADER so the next block's MPT build
/// can read the parent's committed state root through the view (BaselineScheduler parity).
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

/// Resolve the block header's state root: MPT when shouldBuildMPT, otherwise XOR legacy fold.
template <class ViewType>
task::Task<h256> resolveEngineBlockStateRoot(ViewType& view, protocol::BlockHeader& blockHeader,
    ledger::LedgerConfig const& ledgerConfig, crypto::Hash const& hashImpl,
    protocol::BlockFactory& blockFactory)
{
    auto const blockNumber = blockHeader.number();
    if (scheduler_v1::shouldBuildMPT(ledgerConfig.features(), blockNumber))
    {
        scheduler_v1::rejectRawAddressWithMPT(ledgerConfig.features(), blockNumber);
        auto mptDelta =
            co_await buildMPTStateRootForView(view, blockHeader, ledgerConfig, blockFactory);
        blockHeader.setStateRoot(mptDelta.stateRoot);
        co_await publishPendingBlockHeaderForMPT(view, blockHeader);
        co_return mptDelta.stateRoot;
    }
    auto stateRoot = co_await xorStateRoot(view, blockHeader.version(), hashImpl);
    blockHeader.setStateRoot(stateRoot);
    co_return stateRoot;
}

}  // namespace bcos::engine::engine_common
