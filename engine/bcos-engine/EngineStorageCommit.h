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
 * @file EngineStorageCommit.h
 * @brief Shared engine commit helpers (queued-layer drain, executable-tx selection, header
 *        commitments) used by the Eth/Op services.
 */

#pragma once

#include <bcos-framework/engine/Errors.h>
#include <bcos-framework/engine/OpForkId.h>
#include <bcos-framework/engine/RawTransactionDispatch.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-framework/protocol/TransactionReceiptNormalize.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-ledger/mpt/EthTrieRoots.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Bloom.h>
#include <bcos-utilities/Common.h>
#include <cstdint>
#include <optional>
#include <vector>

namespace bcos::engine::engine_common
{

/// Merge every remaining queued layer. Empty deque is NotExistsImmutableStorageError
/// and ends the loop. This still merge-alls; discarding abandoned
/// layers needs a coroutine-safe commit serial.
template <class Storage>
task::Task<void> drainQueuedLayers(Storage& storage)
{
    for (;;)
    {
        bool drained = false;
        try
        {
            co_await storage.mergeBackStorage();
            drained = true;
        }
        catch (bcos::storage2::NotExistsImmutableStorageError const&)
        {}
        if (!drained)
        {
            co_return;
        }
    }
}

/// The executable transactions of an execution payload, index-parallel with their EIP-2718 type
/// bytes. Raw-only (forced) entries have no executable form and are skipped. Blob (0x03) and
/// unsupported envelopes fail closed here — through the same isRawTransactionPayloadAdmissible
/// rule the admission paths use — so the receipts-root leaf can never commit a type prefix for
/// an envelope the repo's policy invalidates the whole payload for.
struct ExecutableTransactions
{
    std::vector<protocol::Transaction::Ptr> transactions;
    std::vector<std::uint8_t> types;
};

template <class PayloadTransactions>
ExecutableTransactions collectExecutableTransactions(PayloadTransactions const& payloadTransactions)
{
    ExecutableTransactions out;
    out.transactions.reserve(payloadTransactions.size());
    out.types.reserve(payloadTransactions.size());
    for (auto const& tx : payloadTransactions)
    {
        if (tx.decoded == nullptr)
        {
            continue;
        }
        // Gate on the single authoritative dispatch table, not rawTransactionTypeByte alone:
        // that returns 0x03 for a blob, so a blob would be committed as a 0x03-prefixed
        // receipts-trie leaf instead of invalidating the payload.
        if (!isRawTransactionPayloadAdmissible(dispatchRawTransaction(bcos::ref(tx.raw))))
        {
            BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
                                      "execution payload carries an inadmissible transaction "
                                      "envelope (blob or unsupported)"});
        }
        out.transactions.push_back(tx.decoded);
        out.types.push_back(*rawTransactionTypeByte(bcos::ref(tx.raw)));
    }
    return out;
}

/// The Ethereum header commitments over a block's transactions and receipts. Single source for
/// both engine services: the leaf encoder, the trie construction and the receipt normalization
/// must not drift, or the two producers commit different block hashes for the same block.
struct HeaderCommitments
{
    bcos::h256 transactionsRoot;
    bcos::h256 receiptsRoot;
    /// Derived from the normalized receipts, so they come out of the same call rather
    /// than being recomputed by each caller: a caller looping over `receipts` itself
    /// would silently depend on normalizeReceipts having already run, on fields that
    /// enter the block hash.
    /// Value-initialized: `HeaderCommitments out;` default-initializes, and an
    /// uninitialized Bloom would start the block bloom's OR from stack garbage — which
    /// reads as zero on one platform and not on another.
    bcos::u256 gasUsed{};
    bcos::Bloom logsBloom{};
};

/// Normalizes @p receipts in place via protocol::normalizeReceipts (same policy as
/// BaselineScheduler::finishExecute) — @p receipts is a non-const reference so the
/// signature states that mutation — then builds both index-keyed MPT roots and the
/// block-level gasUsed / logsBloom.
/// One receipt per executed transaction — the leaf prefix is the transaction's type
/// byte — so a count mismatch fails closed instead of indexing @p types out of range.
/// Forced (decoded == nullptr) envelopes still enter transactionsRoot and do not
/// produce receipts; receiptsRoot is therefore not externally verifiable until
/// deposit execution lands (N envelopes vs M receipts).
/// @p opFork is the block's OP fork when the lane executes deposits (the OP
/// executor seals through encodeReceiptForRoot with the same fork), std::nullopt
/// on a lane where deposits never execute — there a deposit receipt can only be
/// a scheduler contract violation, and fails closed below.
template <class PayloadTransactions>
HeaderCommitments buildHeaderCommitments(PayloadTransactions const& payloadTransactions,
    std::vector<protocol::TransactionReceipt::Ptr>& receipts,
    std::vector<std::uint8_t> const& types, std::optional<bcos::engine::OpForkId> opFork)
{
    for (auto const& receipt : receipts)
    {
        if (!receipt)
        {
            BOOST_THROW_EXCEPTION(OpExecutionInternalError{}
                                  << bcos::errinfo_comment{"scheduler returned a null receipt"});
        }
    }
    protocol::normalizeReceipts(receipts);

    std::vector<bcos::bytesConstRef> rawEnvelopes;
    rawEnvelopes.reserve(payloadTransactions.size());
    for (auto const& tx : payloadTransactions)
    {
        rawEnvelopes.emplace_back(bcos::ref(tx.raw));
    }

    HeaderCommitments out;
    // calculateTransactionsRoot owns the empty-list -> emptyRootHash() contract
    // (computeIndexedTrieRoot), so no caller re-implements it.
    out.transactionsRoot = bcos::ledger::mpt::calculateTransactionsRoot(rawEnvelopes);
    for (auto const& receipt : receipts)
    {
        out.gasUsed += receipt->gasUsed();
        // No empty-bloom guard: normalizeReceipts recomputed every bloom from logEntries, so
        // each is the full 256 bytes orBloom reads.
        bcos::orBloom(out.logsBloom, receipt->logsBloom());
    }

    if (receipts.size() != types.size())
    {
        BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
                                  "scheduler returned a receipt count that does not match the "
                                  "executed transactions"});
    }
    std::vector<bcos::bytes> receiptLeaves;
    receiptLeaves.reserve(receipts.size());
    for (std::size_t i = 0; i < receipts.size(); ++i)
    {
        // op-geth Receipts.EncodeIndex keys the deposit leaf shape on the fork:
        // Canyon+ -> 0x7e || rlp([..., nonce, version]); pre-Canyon -> both omitted.
        // The selector here is the block's fork — the same key the executor's seal
        // (encodeReceiptForRoot) uses — never the receipt's own metadata, so a
        // receipt whose meta disagrees with the fork fails closed instead of
        // silently encoding a leaf the executor would never produce.
        constexpr std::uint8_t c_depositTxType = 0x7e;  // kDepositTxType (OpTransition.h)
        bool includeDepositNonceVersion = true;
        if (types[i] == c_depositTxType)
        {
            if (!opFork.has_value())
            {
                BOOST_THROW_EXCEPTION(
                    OpExecutionInternalError{} << bcos::errinfo_comment{
                        "scheduler returned a deposit receipt on a lane that never "
                        "executes deposits"});
            }
            const bool wantsVersion = *opFork >= bcos::engine::OpForkId::Canyon;
            const auto& meta = receipts[i]->opStackMeta();
            if (!meta || meta->deposit_receipt_version.has_value() != wantsVersion)
            {
                BOOST_THROW_EXCEPTION(
                    OpExecutionInternalError{} << bcos::errinfo_comment{
                        "scheduler returned a deposit receipt whose nonce/version metadata "
                        "does not match the block's fork"});
            }
            if (wantsVersion && !meta->deposit_nonce)
            {
                BOOST_THROW_EXCEPTION(
                    OpExecutionInternalError{} << bcos::errinfo_comment{
                        "scheduler returned a Canyon+ deposit receipt without a deposit "
                        "nonce"});
            }
            includeDepositNonceVersion = wantsVersion;
        }
        receiptLeaves.push_back(bcos::ledger::mpt::encodeReceiptLeaf(
            *receipts[i], types[i], includeDepositNonceVersion));
    }
    std::vector<bcos::bytesConstRef> receiptLeafRefs;
    receiptLeafRefs.reserve(receiptLeaves.size());
    for (auto const& leaf : receiptLeaves)
    {
        receiptLeafRefs.emplace_back(leaf.data(), leaf.size());
    }
    out.receiptsRoot = bcos::ledger::mpt::calculateReceiptsRoot(receiptLeafRefs);
    return out;
}

}  // namespace bcos::engine::engine_common
