/*
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief interface of Scheduler
 * @file SchedulerInterface.h
 * @author: ancelmo
 * @date: 2021-07-27
 */

#pragma once
#include "../ledger/LedgerConfig.h"
#include "../protocol/Block.h"
#include "SchedulerTypeDef.h"
#include "bcos-task/Task.h"
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-utilities/Error.h>
#include <functional>
#include <memory>
#include <string_view>

namespace bcos::scheduler
{
class SchedulerInterface
{
public:
    using Ptr = std::shared_ptr<SchedulerInterface>;
    SchedulerInterface() = default;
    virtual ~SchedulerInterface() noexcept = default;

    // by pbft & sync
    virtual void executeBlock(bcos::protocol::Block::Ptr block, bool verify,
        std::function<void(bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool _sysBlock)>
            callback) = 0;

    // by pbft & sync
    virtual void commitBlock(bcos::protocol::BlockHeader::Ptr header,
        std::function<void(bcos::Error::Ptr, bcos::ledger::LedgerConfig::Ptr)> callback) = 0;

    // by console, query committed committing executing
    virtual void status(
        std::function<void(Error::Ptr, bcos::protocol::Session::ConstPtr)> callback) = 0;

    // by rpc
    virtual void call(protocol::Transaction::Ptr tx,
        std::function<void(Error::Ptr, protocol::TransactionReceipt::Ptr)>) = 0;

    // by rpc: eth_call pinned at a block height (M13.2). The default implementation ignores
    // the height and serves the latest state — byte-for-byte the pre-existing behaviour of
    // every implementation with no historical state to offer (tars SchedulerService, fakes),
    // which is why this is a defaulted method and not a pure one (same pattern as
    // FrontInterface::broadcastMessageByOwnedPayload). BaselineScheduler overrides it
    // with a real execution against the MPT state committed at that block. A distinct name
    // rather than a call() overload: -Woverloaded-virtual (in -Wall, promoted by -Werror)
    // would otherwise fire in every subclass that overrides only the latest-state call().
    virtual void callAtBlock(protocol::Transaction::Ptr tx,
        bcos::protocol::BlockNumber /*blockNumber*/,
        std::function<void(Error::Ptr, protocol::TransactionReceipt::Ptr)> callback)
    {
        call(std::move(tx), std::move(callback));
    }

    // Adopt a verify=false probe as the pending block without re-executing.
    // Default re-runs executeBlock(verify=true); OpScheduler overrides this.
    virtual void adoptProbeAsPending(bcos::protocol::Block::Ptr block,
        std::function<void(bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool _sysBlock)>
            callback)
    {
        executeBlock(std::move(block), true, std::move(callback));
    }

    // clear all status
    virtual void reset(std::function<void(Error::Ptr)> callback) = 0;
    virtual void getCode(
        std::string_view contract, std::function<void(Error::Ptr, bcos::bytes)> callback) = 0;

    virtual void getABI(
        std::string_view contract, std::function<void(Error::Ptr, std::string)> callback) = 0;

    // One key from the uncommitted pending plane (sealed, not yet commitBlock'd).
    // `number` is the ledger-config / feature-flag height for implementations that
    // need one (BaselineScheduler). The pending rows themselves have no historical
    // block context: OpScheduler discards `number` and reads the committed tip's
    // flags. EthEndpoint currently passes 0.
    virtual task::Task<std::optional<bcos::storage::Entry>> getPendingStorageAt(
        std::string_view address, std::string_view key, bcos::protocol::BlockNumber number) = 0;

    // for performance, do the things before executing block in executor.
    virtual void preExecuteBlock(bcos::protocol::Block::Ptr block, bool verify,
        std::function<void(Error::Ptr)> callback) = 0;

    virtual void stop(){};
    virtual void setVersion(int version, ledger::LedgerConfig::Ptr ledgerConfig){};

    // OP lane only (OpEngineService::newPayload): execute @p block on the parent
    // block's post-state WITHOUT any canonical-table write (no NUMBER_2_HASH / no
    // SYS_CURRENT_STATE / no prewriteBlockToBuffer / no pending slot).
    // @p parentFlat is the parent block's materialized post-state, type-erased as
    // shared_ptr<void> across this boundary (the real scheduler casts to its own
    // MultiLayerStorage mutable type; null = parent is the canonical tip). Receipts
    // are attached to @p block; the block's own storage delta returns type-erased
    // for the caller's staged import.
    // After the engine's SetCanonical merged an imported chain: watermarks
    // (lastCommitted / lastExecuted) move to @p number so the scheduler's continuity
    // view agrees with the new canonical tip. Default: no-op (Eth).
    virtual void canonicalizedTo(bcos::protocol::BlockNumber number) { (void)number; }

    // Post-condition of a SetCanonical batch: stateRoot(SYS_CURRENT_STATE) must equal
    // the head header's stateRoot. An implementation MUST throw on mismatch — a
    // half-written canonical chain must never be answered VALID. Lives here because
    // the state-root rebuild needs the executor's trie adapter, which implementations
    // of this interface already link. Default: no-op (Eth/baseline schedulers have no
    // import lane).
    virtual void verifyCanonicalStateRoot(const bcos::h256& expectedStateRoot)
    {
        (void)expectedStateRoot;
    }

    // Default: unsupported — the Eth/baseline schedulers have no import lane.
    // @p parentHeaders are the decoded ancestors (genesis-side FIRST) used to seed
    // NUMBER_2_HASH / NUMBER_2_BLOCK_HEADER for the parent chain, so BLOCKHASH and
    // parent-header reads walk the payload chain, not the canonical tables.
    // The callback's @p blockFlat is the MATERIALIZED post-state of @p block, restored
    // wholesale when an imported head later replaces canonical heights. @p parentFlat
    // is the PARENT block's materialized post-state (null when the parent is the
    // canonical tip, where the committed flat already IS the parent plane): the import
    // plane erases the committed rows and re-materializes parentFlat, so the block
    // executes exactly on its parent's post-state — required for canonical-ancestor
    // siblings whose parent plane differs from the tip.
    //
    // All three type-erased payloads (@p parentFlat, @p blockDelta, @p blockFlat) are
    // shared_ptr to the scheduler's MultiLayerStorage MutableStorage — the concrete
    // type the scheduler and its OP-lane caller agree on. This framework header cannot
    // name that type (the dependency direction is exactly why the signature erases),
    // so the consumer un-erases with static_pointer_cast and a producer that stores
    // any other type is silent UB, not a catchable error. Baseline schedulers never
    // produce these payloads: the default implementation answers UnknownError.
    virtual void importExecute(bcos::protocol::Block::Ptr block,
        std::vector<bcos::protocol::BlockHeader::Ptr> const& parentHeaders,
        std::shared_ptr<void> const& parentFlat,
        std::function<void(Error::Ptr, bcos::protocol::BlockHeader::Ptr,
            std::shared_ptr<void> blockDelta, std::shared_ptr<void> blockFlat)>
            callback)
    {
        callback(BCOS_ERROR_PTR(scheduler::SchedulerError::UnknownError,
                     "importExecute is not supported by this scheduler"),
            nullptr, nullptr, nullptr);
    }
};
}  // namespace bcos::scheduler
