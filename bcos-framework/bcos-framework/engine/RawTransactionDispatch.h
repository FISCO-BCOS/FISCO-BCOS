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

namespace bcos::engine
{

/// Category of a raw EIP-2718 transaction envelope, decided by its first byte.
enum class RawTransactionKind : std::uint8_t
{
    Legacy,       ///< first byte >= 0xc0 (RLP list header)
    AccessList,   ///< 0x01 (EIP-2930)
    DynamicFee,   ///< 0x02 (EIP-1559)
    Blob,         ///< 0x03 (EIP-4844) — parseable but always rejected on L2
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

}  // namespace bcos::engine
