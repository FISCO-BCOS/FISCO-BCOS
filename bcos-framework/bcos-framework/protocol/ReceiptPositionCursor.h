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
 * @file ReceiptPositionCursor.h
 * @brief The receipt-numbering rule, with no dependency on Block.
 *
 * transactionIndex and logIndex are the only receipt fields this defines, and they are pure
 * arithmetic over two counters: the receipt's position in the block and the running log total. Both
 * the write path (ReceiptNormalizer, reached from the engine and the schedulers) and the read path
 * (the derivation in ReceiptBlockPosition.h) apply that same rule, so it lives in its own header
 * with nothing but <cstddef> -- a producer that needs only the numbering must not pull in Block.h
 * and range-v3's any_view to get ten lines of arithmetic.
 */
#pragma once

#include <cstddef>

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
/// The single implementation, shared by the read-time derivation and by ReceiptNormalizer's write.
/// Those two are the same rule applied at different times, so they must not carry a copy of the
/// arithmetic each -- a comment saying they agree is not a guarantee.
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
}  // namespace bcos::protocol
