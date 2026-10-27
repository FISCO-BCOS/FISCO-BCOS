/*
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
 * @brief Shared receipt-field policy for engine and PBFT commit
 * @file TransactionReceiptNormalize.h
 */
#pragma once

#include "Block.h"
#include "TransactionReceipt.h"
#include <bcos-utilities/Bloom.h>
#include <bcos-utilities/Common.h>
#include <concepts>
#include <cstddef>
#include <ranges>

namespace bcos::protocol
{

/// Receipt-normalization policy shared by BaselineScheduler::finishExecute, engine
/// buildHeaderCommitments, and the OP block path (opstack-executor finalizeOpBlockResult and
/// processOpBlock call it before sealOpBlock). Callers must reject null receipts before entry.
///
/// - transactionIndex / logIndex are always written (the first log of receipt i
///   is numbered after all logs of receipts 0..i-1).
/// - logsBloom is always recomputed from logEntries. It is a pure function of them, and
///   the only in-tree producer of a receipt bloom (the OP lane's receipt construction in
///   opstack-executor/OpExecutionPolicy.h, successor of the retired bcos-evm/opstack
///   makeFiscoReceipt) derives both the bloom and the log entries from the same evmone
///   receipt, with a 1:1 log mapping — so no producer-supplied value can legitimately
///   differ. Matching the base finishExecute, which also recomputed it unconditionally.
/// - cumulativeGasUsed is filled when empty; a scheduler-provided value stays (the
///   running prefix is a scheduling decision, not a derived field).
/// The numbering/bloom policy, as a reusable cursor, so the two entry points below cannot drift.
///
/// `apply` takes the receipt by reference: for Block::receipts() that is the holder itself, which
/// must stay alive across the call (it is the holder that owns the pointer to the stored receipt).
struct ReceiptNormalizer
{
    u256 cumulativeGasUsed{};
    size_t logIndex = 0;
    size_t index = 0;

    template <class Receipt>
    void apply(Receipt& receipt)
    {
        receipt->setTransactionIndex(index);
        receipt->setLogIndex(logIndex);
        auto const bloom = bcos::getLogsBloom(receipt->logEntries());
        receipt->setLogsBloom(bcos::bytesConstRef(bloom.data(), bloom.size()));
        logIndex += receipt->logEntries().size();
        cumulativeGasUsed += receipt->gasUsed();
        if (receipt->cumulativeGasUsed().empty())
        {
            receipt->setCumulativeGasUsed(cumulativeGasUsed.str());
        }
        ++index;
    }
};

/// Normalize a range of receipt pointers (the shape every scheduler-path caller has).
///
/// Constrained to input ranges on purpose. Without the constraint this template out-ranks the
/// Block overload below for a non-const `Block&`: it would deduce `Receipts = Block`, bind
/// `Block&` (a better match than `Block const&`), and then fail to instantiate because
/// protocol::Block has no begin/end. A caller holding a `Block::Ptr` -- the shape the scheduler
/// and the engine use -- would get an error from inside this header with no hint that an
/// overload exists for exactly their case.
template <class Receipts>
    requires ::ranges::input_range<Receipts>
inline u256 normalizeReceipts(Receipts& receipts)
{
    ReceiptNormalizer normalizer;
    for (auto& receipt : receipts)
    {
        normalizer.apply(receipt);
    }
    return normalizer.cumulativeGasUsed;
}

/// Normalize a block's own receipts in place.
///
/// Block::receipts() yields a holder per receipt BY VALUE, so the range cannot be passed to the
/// overload above (its elements are const temporaries). Iterating and applying per element keeps
/// each holder alive for exactly as long as it is used, and the holder writes through to the
/// receipt the block stores -- the same shape setReceipt relies on.
///
/// Takes the block by const reference because the receipts, not the block's container, are what
/// change: BlockImpl keeps its tars inner mutable and its receipts() builds holders over that
/// mutable storage, so writing through one obtained from a const Block is not a const_cast.
inline u256 normalizeReceipts(Block const& block)
{
    ReceiptNormalizer normalizer;
    for (auto receipt : block.receipts())  // by value: the holder owns the pointer while it lives
    {
        normalizer.apply(receipt);
    }
    return normalizer.cumulativeGasUsed;
}

}  // namespace bcos::protocol
