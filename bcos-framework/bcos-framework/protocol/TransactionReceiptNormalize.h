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
#include <cstddef>

namespace bcos::protocol
{

/// Single receipt-normalization policy used by BaselineScheduler::finishExecute and
/// engine buildHeaderCommitments. Callers must reject null receipts before entry.
///
/// - transactionIndex / logIndex are always written (the first log of receipt i
///   is numbered after all logs of receipts 0..i-1).
/// - logsBloom is always recomputed from logEntries. It is a pure function of them, and
///   the only in-tree producer of a receipt bloom (bcos-evm/opstack/OpTransition.cpp
///   makeFiscoReceipt) derives both the bloom and the log entries from the same evmone
///   receipt, with a 1:1 log mapping — so no producer-supplied value can legitimately
///   differ. Matching the base finishExecute, which also recomputed it unconditionally.
/// - cumulativeGasUsed is filled when empty; a scheduler-provided value stays (the
///   running prefix is a scheduling decision, not a derived field).
template <class Receipts>
inline u256 normalizeReceipts(Receipts& receipts)
{
    u256 cumulativeGasUsed = 0;
    size_t logIndex = 0;
    size_t index = 0;
    for (auto& receipt : receipts)
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
    return cumulativeGasUsed;
}

}  // namespace bcos::protocol
