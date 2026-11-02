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
 * @file ReceiptBlockPosition.h
 * @brief Derive a receipt's block position from the block, instead of reading it back from storage.
 *
 * transactionIndex and logIndex are pure functions of the block: the receipt's position among the
 * block's transactions, and the number of logs in the receipts before it. Neither is committed by
 * any root -- the tars fields live on the outer TransactionReceipt, while TarsHashable hashes only
 * `receipt.data`, and the Ethereum-lane receiptsRoot covers (status, cumulativeGasUsed, logsBloom,
 * logs) -- so this derivation cannot disagree with anything the chain committed.
 *
 * Deriving is what makes the two ways a node can hold the same block agree. The stored value is
 * producer-dependent: a block built through BaselineScheduler carries real indices, while one
 * written by the serial driver -- and one a resync re-executes -- carries the struct default 0.
 * Repairing that on write cannot be done without telling resyncs and upgrades apart, which no
 * on-chain signal can do. Deriving at read time gives every node one answer without rewriting a
 * single stored row: no backfill, no activation marker, no lane predicate.
 *
 * The derived position is therefore the authoritative one, and every RPC reader stamps it before
 * publishing. The producer keeps writing its own value (ReceiptNormalizer), which is why the
 * arithmetic lives in ReceiptPositionCursor and nothing else: one cursor, two writers, no drift.
 */
#pragma once

#include "Block.h"
#include "TransactionReceipt.h"
#include <algorithm>
#include <cstddef>
#include <optional>
#include <vector>

namespace bcos::protocol
{
/// Where one receipt sits in its block.
struct ReceiptBlockPosition
{
    std::size_t transactionIndex = 0;
    /// Number of logs in the receipts before this one, so its first log is numbered from here.
    std::size_t logIndex = 0;
};

/// The numbering rule itself: the receipt at position i of a block has transactionIndex i and
/// logIndex equal to the number of logs in receipts 0..i-1.
///
/// The single implementation, shared by the read-time derivation below and by ReceiptNormalizer's
/// write. Those two are the same rule applied at different times, so they must not carry a copy of
/// the arithmetic each -- a comment saying they agree is not a guarantee.
struct ReceiptPositionCursor
{
    /// Take the next position for a receipt carrying @p logCount log entries, then advance.
    ReceiptBlockPosition next(std::size_t logCount)
    {
        auto const position = ReceiptBlockPosition{m_transactionIndex, m_logIndex};
        ++m_transactionIndex;
        m_logIndex += logCount;
        return position;
    }

private:
    std::size_t m_transactionIndex = 0;
    std::size_t m_logIndex = 0;
};

/// Derive the block position of every receipt of @p block, in block order, and stamp it onto them.
///
/// Block::receipts() hands out a holder per receipt that writes through to the receipt the block
/// stores, so stamping once makes every later read of the block consistent -- including the paths
/// (eth_getLogs, the event-sub channel) that read the fields off receipts they iterate themselves.
///
/// The returned vector is parallel to the block's receipt order, so a caller that needs one
/// receipt's position (eth_getTransactionReceipt) indexes it rather than re-deriving.
inline std::vector<ReceiptBlockPosition> stampReceiptBlockPositions(protocol::Block const& block)
{
    std::vector<ReceiptBlockPosition> positions;
    positions.reserve(block.receiptsSize());
    ReceiptPositionCursor cursor;
    for (auto receipt : block.receipts())  // by value: the holder owns the pointer while it lives
    {
        auto const position = cursor.next(receipt->logEntries().size());
        receipt->setTransactionIndex(position.transactionIndex);
        receipt->setLogIndex(position.logIndex);
        positions.push_back(position);
    }
    return positions;
}

/// Copy a derived position onto a receipt read separately from the block.
inline void applyReceiptBlockPosition(
    protocol::TransactionReceipt& receipt, ReceiptBlockPosition const& position)
{
    receipt.setTransactionIndex(position.transactionIndex);
    receipt.setLogIndex(position.logIndex);
}

namespace detail
{
/// Visit the block's transaction hashes in block order, whichever list the read populated.
///
/// A block read carries its transactions in one of two lists and never both:
/// TRANSACTIONS_HASH fills transactionMetaDatas() and leaves transactions() empty, TRANSACTIONS
/// does the opposite (LedgerMethods.h::getBlockDataFromStorages appends to one or the other). The
/// receipts pair one-to-one with whichever is present, so anything keyed by a transaction hash has
/// to look in both. Stops early when @p visit returns true.
template <class Visit>
void visitTransactionHashes(protocol::Block const& block, Visit&& visit)
{
    if (block.transactionsMetaDataSize() != 0)
    {
        // transactionMetaDatas() is a ranges::any_view over the block's mutable inner storage, so
        // the holders it yields are writable; only their hashes are read here.
        std::size_t index = 0;
        for (auto const& meta : block.transactionMetaDatas())
        {
            if (visit(meta->hash(), index))
            {
                return;
            }
            ++index;
        }
        return;
    }
    std::size_t index = 0;
    for (auto const& transaction : block.transactions())
    {
        if (visit(transaction->hash(), index))
        {
            return;
        }
        ++index;
    }
}
}  // namespace detail

/// The index of @p txHash in the block's transaction list, or nullopt when the block does not list
/// it. This is all eth_getTransactionByHash needs to publish transactionIndex, and unlike
/// findReceiptPosition it does not read the receipts -- so a caller that only wants the index does
/// not pay for N receipt decodes.
inline std::optional<std::size_t> findTransactionIndex(
    protocol::Block const& block, crypto::HashType const& txHash)
{
    std::optional<std::size_t> found;
    detail::visitTransactionHashes(block, [&](crypto::HashType const& hash, std::size_t index) {
        if (hash != txHash)
        {
            return false;
        }
        found = index;
        return true;
    });
    return found;
}

/// The derived position of the receipt produced by @p txHash, or nullopt when the block does not
/// list that transaction (the stored rows then disagree with the block).
///
/// One implementation for every endpoint that resolves a receipt by hash: the block's transaction
/// list and its receipts are written in the same order, so the position is the index of the hash in
/// that list, and the log base is the count of logs before it. Requires the block to have been read
/// with RECEIPTS -- logIndex is the reason a receipt lookup needs its predecessors at all.
inline std::optional<ReceiptBlockPosition> findReceiptPosition(
    protocol::Block const& block, crypto::HashType const& txHash)
{
    auto const positions = stampReceiptBlockPositions(block);
    auto const transactionCount =
        std::max(static_cast<std::size_t>(block.transactionsMetaDataSize()),
            static_cast<std::size_t>(block.transactionsSize()));
    // The receipts pair one-to-one with the transactions. A mismatch means the stored rows disagree
    // with the block (or the block was read without RECEIPTS), which is what nullopt reports.
    if (positions.size() != transactionCount)
    {
        return std::nullopt;
    }
    std::optional<ReceiptBlockPosition> found;
    detail::visitTransactionHashes(block, [&](crypto::HashType const& hash, std::size_t index) {
        if (hash != txHash)
        {
            return false;
        }
        found = positions[index];
        return true;
    });
    return found;
}
}  // namespace bcos::protocol
