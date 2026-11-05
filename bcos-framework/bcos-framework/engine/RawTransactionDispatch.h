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
 * @file RawTransactionDispatch.h
 * @brief First-byte dispatch table for raw EIP-2718 transaction envelopes
 */

#pragma once

#include "bcos-utilities/Common.h"
#include <cstdint>
#include <optional>

namespace bcos::engine
{

/// Category of a raw EIP-2718 transaction envelope, decided by its first byte.
enum class RawTransactionKind : std::uint8_t
{
    Legacy,       ///< first byte >= 0xc0 (RLP list header)
    AccessList,   ///< 0x01 (EIP-2930)
    DynamicFee,   ///< 0x02 (EIP-1559)
    Blob,         ///< 0x03 (EIP-4844) — admitted only on chains whose BlobPolicy allows it
    SetCode,      ///< 0x04 (EIP-7702)
    Deposit,      ///< 0x7e (OP Stack deposit)
    Unsupported,  ///< everything else, including 0x00 and empty input
};

/// The single authoritative dispatch table for raw transaction bytes. Every entry point
/// that admits raw transactions (eth_sendRawTransaction, the in-process mempool, Engine
/// forkchoiceUpdated attributes.transactions and newPayload payload.transactions) must
/// classify through this function rather than re-implementing first-byte checks.
///
/// Note: 0x00 is NOT a valid EIP-2718 type. Bytes in [0x05, 0x7d] and [0x7f, 0xbf] are
/// reserved/unknown and classify as Unsupported.
inline RawTransactionKind dispatchRawTransaction(bcos::bytesConstRef raw)
{
    if (raw.empty())
    {
        return RawTransactionKind::Unsupported;
    }
    switch (raw[0])
    {
    case 0x01:
        return RawTransactionKind::AccessList;
    case 0x02:
        return RawTransactionKind::DynamicFee;
    case 0x03:
        return RawTransactionKind::Blob;
    case 0x04:
        return RawTransactionKind::SetCode;
    case 0x7e:
        return RawTransactionKind::Deposit;
    default:
        // 0xc0..0xff: RLP list header => legacy transaction.
        return raw[0] >= 0xc0 ? RawTransactionKind::Legacy : RawTransactionKind::Unsupported;
    }
}

/// Whether a transaction of this kind may appear inside an Engine execution payload.
/// FISCO's OP policy rejects blob (type-3) txs at the gate — op-geth's decodeTyped accepts
/// them, so this is a deliberate acceptance divergence, not an op-geth check. A single blob or
/// unsupported transaction invalidates the whole payload, it is not dropped individually.
inline bool isRawTransactionPayloadAdmissible(RawTransactionKind kind)
{
    return kind != RawTransactionKind::Blob && kind != RawTransactionKind::Unsupported;
}

/// The EIP-2718 type byte of a raw envelope, for callers that must reproduce the Ethereum
/// header commitment (the receipt trie leaf's type prefix). Legacy envelopes carry no prefix
/// and map to 0x00. An unsupported envelope has no type byte at all, so it returns nullopt
/// rather than folding into legacy's 0x00 — a commitment builder must fail closed instead of
/// silently committing a legacy-shaped leaf for it.
[[nodiscard]] inline std::optional<std::uint8_t> rawTransactionTypeByte(bcos::bytesConstRef raw)
{
    switch (dispatchRawTransaction(raw))
    {
    case RawTransactionKind::AccessList:
        return 0x01;
    case RawTransactionKind::DynamicFee:
        return 0x02;
    case RawTransactionKind::Blob:
        return 0x03;
    case RawTransactionKind::SetCode:
        return 0x04;
    case RawTransactionKind::Deposit:
        return 0x7e;
    case RawTransactionKind::Legacy:
        return 0x00;
    case RawTransactionKind::Unsupported:
    default:
        return std::nullopt;
    }
}

/// The EIP-2718 type byte that a transaction's receipt-trie leaf is prefixed with.
///
/// Taken from the signed envelope whenever the envelope is classifiable, never from
/// `web3TypedTxKind()` alone: that is a tars field a peer can rewrite (see TransactionImpl), which
/// is why every other type decision in the tree (isDepositTx, EngineStorageCommit's commitment
/// builder) reads the envelope.
///
/// The mirror is consulted whenever the envelope does not classify, which is broader than one case:
/// rawTransactionTypeByte returns nullopt for an empty envelope AND for a first byte of 0x00,
/// 0x05-0x7d or 0x7f-0xbf. Of those, only the empty envelope is reachable here -- it is what
/// FISCO-native and test-built transactions carry -- because admission rejects the rest: the
/// envelope decode in bcos-tx-validator/bcos-tx-validator/Normalize.cpp refuses an unclassifiable
/// Web3 transaction and then rewrites this mirror from the decoded envelope, so the two always
/// agree for anything that got through. That gate, not this function, is what makes the non-empty
/// cases unreachable; a future caller on another path must not assume it.
///
/// The fallback also predates this helper: those transactions already worked through
/// web3TypedTxKind() -- 0 for a non-Web3 transaction -- and throwing here instead would fail them.
///
/// This belongs next to rawTransactionTypeByte because "which type is this envelope" is that
/// module's decision: the committed receiptsRoot, the debug_getRawReceipts preimage and the
/// OP-aware receipt encoder this repo has yet to add must all reach the same answer from here, not
/// through copies. Duck-typed on the transaction so this header keeps its light include set (the
/// callers pass `Transaction const&`, and `AnyTransaction` dereferences to one).
template <class Transaction>
[[nodiscard]] inline std::uint8_t receiptLeafTypeByte(Transaction const& transaction)
{
    if (auto const typeByte = rawTransactionTypeByte(transaction.extraTransactionBytes());
        typeByte.has_value())
    {
        return *typeByte;
    }
    return transaction.web3TypedTxKind();
}

}  // namespace bcos::engine
