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
 * @file EthTrieRoots.h
 * @brief Ethereum block-header trie roots (transactions / receipts / withdrawals) and the
 *        block-level logs bloom — the "non-secure" tries committed to by the header.
 * @date 2026/8/18
 */
#pragma once

#include "HashBuilder.h"
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-utilities/Bloom.h>
#include <bcos-utilities/Common.h>
#include <boost/throw_exception.hpp>
#include <range/v3/range.hpp>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace bcos::ledger::mpt
{

/// Build one Ethereum "index-keyed" trie root — the non-secure transaction / receipt /
/// withdrawal tries in a block header. Key = RLP-encoded integer index
/// (codec::rlp::encode(out, uint64_t(i))), value = the item's pre-encoded RLP byte string.
///
/// @param items  The items' RLP encodings, in index order (0, 1, ...). The keys are re-sorted
///               ascending by ENCODED KEY BYTES internally — rlp(0)=0x80 sorts AFTER
///               rlp(1)=0x01, so this is NOT numeric index order.
/// @return The 32-byte trie root; emptyRootHash() for an empty input.
bcos::h256 computeIndexedTrieRoot(std::span<bcos::bytesConstRef const> items);

/// Transactions trie root (txsRoot): values are the raw EIP-2718 transaction encodings.
inline bcos::h256 calculateTransactionsRoot(std::span<bcos::bytesConstRef const> txs)
{
    return computeIndexedTrieRoot(txs);
}
/// Receipts trie root (receiptsRoot): values are the receipt RLP encodings
/// (EthReceipt::rlpEncode output — type-prefix + [status, cumGas, logsBloom, logs]).
inline bcos::h256 calculateReceiptsRoot(std::span<bcos::bytesConstRef const> receipts)
{
    return computeIndexedTrieRoot(receipts);
}
/// Withdrawals trie root (withdrawalsRoot, Shanghai+): values are the withdrawal RLP encodings.
inline bcos::h256 calculateWithdrawalsRoot(std::span<bcos::bytesConstRef const> withdrawals)
{
    return computeIndexedTrieRoot(withdrawals);
}

/// Block-level logs bloom: bitwise OR of the per-receipt 256-byte blooms (each computed from its
/// logs via bcos::getLogsBloom). Returns a zero bloom for an empty input. Forwarding reference:
/// the range is only read, never copied (a bcos::Bloom is 256 bytes, so a by-value parameter
/// would copy the whole vector for an lvalue caller). The element type is pinned to bcos::Bloom
/// (not just any 1-byte-contiguous range): orBloom reads 256 bytes unconditionally, so a
/// shorter buffer — e.g. a bcos::bytes decoded from receipt RLP — would be read past its end.
template <::ranges::input_range Blooms>
    requires std::same_as<std::remove_cvref_t<::ranges::range_value_t<Blooms>>, bcos::Bloom>
bcos::Bloom calculateLogsBloom(Blooms&& blooms)
{
    bcos::Bloom result{};
    for (auto const& bloom : blooms)
    {
        bcos::orBloom(result, bloom);
    }
    return result;
}

/// Finalize executed receipts for commit: stamp transactionIndex / logIndex, recompute the
/// logsBloom from the receipt's log entries, and accumulate cumulativeGasUsed. The bloom is
/// ALWAYS recomputed from logEntries, never taken from the producer — a wrong producer-filled
/// bloom would otherwise be silently committed into the receipts trie and the block-level
/// bloom. The logIndex stamp takes effect only on receipt implementations that persist it;
/// on the tars receipt (the only production receipt type) setLogIndex is currently a no-op —
/// logIndex() is hardcoded to 0 — tracked as a follow-up (the tars receipt needs the field).
/// Throws std::runtime_error on a null receipt; both callers (BaselineScheduler::finishExecute
/// and the engine's buildPayload) already guard nulls at the call site before any dereference,
/// so this throw is defense-in-depth. Shared by the two so they cannot drift —
/// cumulativeGasUsed and the bloom feed the receipts trie, a consensus field.
/// @return The block's totalGasUsed (the last receipt's cumulativeGasUsed).
template <::ranges::input_range Receipts>
bcos::u256 finalizeReceipts(Receipts& receipts)
{
    bcos::u256 totalGasUsed;
    size_t transactionIndex = 0;
    size_t logIndex = 0;
    for (auto const& receipt : receipts)
    {
        if (!receipt)
        {
            BOOST_THROW_EXCEPTION(std::runtime_error{"Null receipt returned by scheduler"});
        }
        receipt->setTransactionIndex(transactionIndex++);
        receipt->setLogIndex(logIndex);
        auto logBloom = bcos::getLogsBloom(receipt->logEntries());
        receipt->setLogsBloom({logBloom.data(), logBloom.size()});
        logIndex += receipt->logEntries().size();
        totalGasUsed += receipt->gasUsed();
        receipt->setCumulativeGasUsed(totalGasUsed.str());
    }
    return totalGasUsed;
}

}  // namespace bcos::ledger::mpt
