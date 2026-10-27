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
/// The numbering/bloom policy, as a reusable cursor, so the entry points below cannot drift.
///
/// `apply` takes the element by reference, not the dereferenced receipt: a container of holders
/// (Block::receipts() yields one holder per receipt by value) must keep that holder alive across
/// the call, because it is the holder that owns the pointer to the stored receipt.
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
/// Constrained to input ranges so a mistaken non-range argument fails with a clear "constraints
/// not satisfied" instead of instantiating the body: without it, anything with a `receipts()`
/// method (a `Block`, say) would bind `Receipts` to that type and only then fail inside the
/// range-for, pointing at a line the caller never wrote.
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

}  // namespace bcos::protocol
