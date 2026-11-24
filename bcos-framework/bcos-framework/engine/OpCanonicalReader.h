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
 * @file OpCanonicalReader.h
 * @brief The OP lane's read facade over the unfinalized window (D1 §10.2).
 */
#pragma once

#include "../protocol/Block.h"
#include "../protocol/ProtocolTypeDef.h"
#include "../protocol/Transaction.h"
#include "../protocol/TransactionReceipt.h"
#include "../storage2/AnyStorage.h"
#include "../transaction-executor/StateKey.h"
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <memory>
#include <optional>

namespace bcos::engine
{

/// What the RPC read plane consults on the OP Engine lane once blocks stay in memory until
/// op-node finalizes them (D1 §10.2, rules 1 and 2): every VIEW comes from the OpScheduler's
/// window (viewAt / hashAtHeightOnChain), every HEIGHT from the Engine tracker. The backend
/// holds the finalized chain only, so:
///
///   latest    = the tracker's head; the finalized tip until the first forkchoiceUpdated after
///               a restart (D1 §13.2), so the three tags agree and op-node refills from there;
///   safe      = the tracker's safe block when it is on the selected chain, else finalized;
///   finalized = the backend tip.
///
/// Captured reads use the pinned backend up to the captured finalized height and the selected
/// window layers above it. A window block that is not on the head chain is still readable by
/// hash (op-node's backup-unsafe restore walks it) but never by number.
///
/// Type-erased so bcos-rpc names no scheduler or engine template; the concrete
/// implementation lives in engine/bcos-engine/OpCanonicalReaderImpl.h and is wired by the
/// composition root (EngineServiceInitializer::buildOp). Null on every other lane.
class OpCanonicalReader
{
public:
    using Ptr = std::shared_ptr<OpCanonicalReader>;
    /// The same handle shapes NodeService takes for the committed plane, so the endpoints
    /// swap the plane, not the code that reads it.
    using MPTNodeReader = storage2::AnyStorage<bcos::h256, bcos::bytes>;
    using StateStorage = storage2::AnyStorage<executor_v1::StateKey, executor_v1::StateValue>;

    struct BlockRef
    {
        crypto::HashType hash;
        protocol::BlockNumber number = 0;
    };

    /// The selected chain's ledger rows and trie nodes. Unfinalized rows stay in retained
    /// layers, and the finalized prefix stays on the captured backend snapshot.
    class WindowReadView
    {
    public:
        virtual ~WindowReadView() = default;
        virtual task::Task<protocol::Block::Ptr> block(
            protocol::BlockNumber number, int32_t flags) = 0;
        virtual task::Task<std::optional<protocol::BlockNumber>> blockNumber(
            crypto::HashType hash) = 0;
        /// Transfer ownership to a reader after the caller has read the block header.
        virtual std::shared_ptr<MPTNodeReader> takeMptNodeReader(
            std::shared_ptr<StateStorage>* state = nullptr) = 0;
        virtual std::shared_ptr<StateStorage> takeStateStorage() = 0;
    };

    struct HeadReadView
    {
        protocol::BlockNumber head;
        protocol::BlockNumber safe;
        protocol::BlockNumber finalized;
        std::unique_ptr<WindowReadView> window;
    };

    struct WindowBlockView
    {
        protocol::BlockNumber number;
        std::unique_ptr<WindowReadView> window;
    };

    /// A transaction resolved on the head chain with the rows eth_getTransactionByHash /
    /// eth_getTransactionReceipt render, including block rows requested for its position.
    struct ChainTransaction
    {
        protocol::Transaction::Ptr transaction;
        protocol::TransactionReceipt::Ptr receipt;
        crypto::HashType blockHash;
        protocol::Block::Ptr block{};
    };

    virtual ~OpCanonicalReader() = default;

    /// The finalized (backend) tip.
    virtual task::Task<BlockRef> finalized() = 0;
    /// The tracker's head, else finalized() (empty tracker after a restart).
    virtual task::Task<BlockRef> head() = 0;
    /// The tracker's safe height if on the selected chain, else finalized().number.
    virtual task::Task<protocol::BlockNumber> safeNumber() = 0;

    /// Capture the head and its tag resolution together with the view for all selected heights.
    virtual task::Task<HeadReadView> captureHeadView() = 0;
    /// Capture a specific window block and its view together, including non-head branches.
    virtual task::Task<std::optional<WindowBlockView>> captureWindowView(
        crypto::HashType const& hash) = 0;

    /// The hash at @p number on the HEAD chain: the window walk from head(), then the ledger's
    /// SYS_NUMBER_2_HASH once the height is finalized. nullopt above the head.
    virtual task::Task<std::optional<crypto::HashType>> canonicalHashAt(
        protocol::BlockNumber number) = 0;

    /// The height of an unfinalized (window) block, any branch; nullopt when @p blockHash is
    /// not in the window (finalized or unknown).
    /// Called concurrently with window writes. Use a short, synchronized metadata lookup
    /// with no I/O or awaiting other work under the window lock. The result does not pin
    /// the window entry for subsequent reads.
    virtual std::optional<protocol::BlockNumber> unfinalizedNumberOf(
        crypto::HashType const& blockHash) const = 0;

    /// The block rows (ledger::getBlockData flags) of an unfinalized block, decoded from its
    /// own chain view — its header, transactions and receipts live in its window layer
    /// (D1 §9.1). Null when @p blockHash is not in the window.
    /// Return a fresh block and receipts for each read: RPC stamps positions and consumes
    /// receipt logs while rendering the result.
    virtual task::Task<protocol::Block::Ptr> unfinalizedBlock(
        crypto::HashType const& blockHash, int32_t blockFlag) = 0;

    /// A transaction by hash on the head chain's view (window layers over the backend);
    /// nullopt when the head chain does not carry it — a transaction that exists only on a
    /// side branch is NOT answered (eth_getTransactionByHash then returns null, as geth does
    /// for a non-canonical inclusion).
    /// Non-zero @p blockFlag loads block rows from the same view as the transaction and
    /// receipt. The decoded objects belong to this read and may be mutated by the caller.
    virtual task::Task<std::optional<ChainTransaction>> transactionOnHeadChain(
        crypto::HashType const& txHash, int32_t blockFlag = 0) = 0;

    /// MPT node reader over the chain view of @p blockHash (a window block, the finalized
    /// tip, or any canonical finalized block below the tip): the trie nodes of that chain's
    /// heights, so one reader at head() serves any height on the head chain and historical
    /// canonical finalized blocks stay provable across finalization. Null when @p blockHash
    /// resolves to no view.
    virtual task::Task<std::shared_ptr<MPTNodeReader>> mptNodeReaderAt(
        crypto::HashType const& blockHash) = 0;

    /// Flat state view of the chain of @p blockHash (a window block or the finalized tip;
    /// historical finalized heights below the tip return null): the plane eth_getStorageAt's
    /// flat read, the content-addressed code store and the mempool snapshot consult on the
    /// OP lane.
    virtual task::Task<std::shared_ptr<StateStorage>> stateStorageAt(
        crypto::HashType const& blockHash) = 0;

    /// Capture the current head and its flat state together.
    virtual task::Task<std::shared_ptr<StateStorage>> headStateStorage() = 0;
};

}  // namespace bcos::engine
