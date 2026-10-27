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
 * producer-dependent and is left as each producer wrote it: a block built through
 * BaselineScheduler carries real indices, while one written by the serial driver -- and one a
 * resync re-executes -- carries the struct default 0. Repairing that on write cannot be done
 * without telling resyncs and upgrades apart, which no on-chain signal can do. Deriving at read
 * time gives every node one answer without rewriting a single stored row: no backfill, no
 * activation marker, no lane predicate.
 *
 * The stored value is therefore not authoritative and is never consulted. Only the in-memory
 * receipt objects a block read just decoded are stamped; nothing here writes to storage.
 */
#pragma once

#include "Block.h"
#include "TransactionReceipt.h"
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

/// Derive the block position of every receipt of @p block, in block order, and stamp it onto them.
///
/// Block::receipts() hands out a holder per receipt that writes through to the receipt the block
/// stores, so stamping once makes every later read of the block consistent -- including the paths
/// (eth_getLogs) that read the fields off receipts they iterate themselves.
///
/// The returned vector is parallel to the block's receipt order, so a caller that needs one
/// receipt's position (eth_getTransactionReceipt) indexes it rather than re-deriving.
inline std::vector<ReceiptBlockPosition> stampReceiptBlockPositions(protocol::Block const& block)
{
    std::vector<ReceiptBlockPosition> positions;
    positions.reserve(block.receiptsSize());
    std::size_t logIndex = 0;
    std::size_t transactionIndex = 0;
    for (auto receipt : block.receipts())  // by value: the holder owns the pointer while it lives
    {
        receipt->setTransactionIndex(transactionIndex);
        receipt->setLogIndex(logIndex);
        positions.push_back(ReceiptBlockPosition{transactionIndex, logIndex});
        logIndex += receipt->logEntries().size();
        ++transactionIndex;
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

/// The derived position of the receipt produced by @p txHash, or nullopt when the block does not
/// list that transaction (the stored rows then disagree with the block).
///
/// One implementation for every endpoint that resolves a receipt by hash: the block's transaction
/// list and its receipts are written in the same order, so the position is the index of the hash
/// in that list -- and the log base is the count of logs before it.
inline std::optional<ReceiptBlockPosition> findReceiptPosition(
    protocol::Block& block, crypto::HashType const& txHash)
{
    auto const positions = stampReceiptBlockPositions(block);
    if (positions.size() != block.transactionsSize())
    {
        return std::nullopt;
    }
    std::size_t index = 0;
    // transactionMetaDatas() is a ranges::any_view: its iterator/sentinel pair differs, so it
    // cannot be fed to std::find_if, and the const form is not iterable at all -- hence the
    // non-const Block& parameter.
    auto metas = block.transactionMetaDatas();
    for (auto const& meta : metas)
    {
        if (meta->hash() == txHash)
        {
            return positions[index];
        }
        ++index;
    }
    return std::nullopt;
}
}  // namespace bcos::protocol
