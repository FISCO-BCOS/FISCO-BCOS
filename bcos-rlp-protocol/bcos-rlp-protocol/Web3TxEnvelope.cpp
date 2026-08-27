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
 * @file Web3TxEnvelope.cpp
 * @brief Library TU for web3ChainIdFromEnvelope. Kept out of the public header so every
 *        consumer compiles one copy (link-period identity) rather than one copy per include.
 */
#include "Web3TxEnvelope.h"
#include <bcos-codec/rlp/RLPDecode.h>

namespace bcos::rlp::protocol
{
Web3EnvelopeChainIdResult classifyWeb3EnvelopeChainId(bcos::bytesConstRef payload)
{
    auto const malformed = [] {
        return Web3EnvelopeChainIdResult{.kind = Web3EnvelopeChainIdKind::Malformed};
    };
    auto const unprotected = [] {
        return Web3EnvelopeChainIdResult{.kind = Web3EnvelopeChainIdKind::Unprotected};
    };
    auto const protectedChainId = [](uint64_t chainId) {
        return Web3EnvelopeChainIdResult{
            .kind = Web3EnvelopeChainIdKind::Protected, .chainId = chainId};
    };
    if (payload.empty()) [[unlikely]]
    {
        return malformed();
    }
    auto const firstByte = payload[0];
    // decode() requires a mutable bytesRef cursor even for read-only parsing; the cast is safe
    // because this function never writes through the cursor — only reads via decodeHeader/decode.
    bcos::bytesRef cursor(const_cast<bcos::byte*>(payload.data()), payload.size());
    // 0x7E (Deposit) has no chainId field — field 0 is sourceHash (h256). Classified as its own
    // kind (NOT Malformed, NOT nullopt): chainId gates key a deliberate per-surface policy on
    // this shape instead of re-deriving "deposit vs junk" from first bytes (#5496 finding K).
    if (firstByte == 0x7E) [[unlikely]]
    {
        return {.kind = Web3EnvelopeChainIdKind::Deposit};
    }
    if (firstByte > 0 && firstByte < bcos::codec::rlp::BYTES_HEAD_BASE)
    {
        // Typed (EIP-2718: 0x01-0x04): chainId is RLP field 0 of the inner list. Canonical
        // integer form enforced (#5496 finding N): a leading-zero/non-minimal field encodes
        // fine but strict references reject it, and admitting it here would let pool/RPC/executor
        // disagree with any strict peer on the SAME bytes.
        cursor = cursor.getCroppedData(1);
        auto&& [error, header] = bcos::codec::rlp::decodeHeader(cursor);
        if (error || !header.isList || header.payloadLength > cursor.size()) [[unlikely]]
        {
            return malformed();
        }
        uint64_t chainId = 0;
        if (auto e = decodeCanonicalRlpUint(cursor, chainId); e != nullptr) [[unlikely]]
        {
            return malformed();
        }
        return protectedChainId(chainId);
    }
    // Legacy: walk the first 6 fields. The tail after them is one of two forms:
    //   preimage  EIP-155: [..6 fields, chainId, 0, 0]  (admission path — takeToTarsTransaction
    //             stores encodeForSign())
    //   full      envelope: [..6 fields, v, r, s]       (block path — engine SEV-8 feeds the
    //             raw signed envelope)
    // In the preimage form field 7 IS the chainId and fields 8/9 are the empty 0,0 placeholders;
    // in the full form field 7 is v (27/28 unprotected, or chainId*2+35+yParity) and fields 8/9
    // are the non-empty r/s scalars (EIP-2 keeps r,s in [1,n-1], never empty). Distinguish by
    // whether field 8 is an empty byte string.
    auto&& [error, header] = bcos::codec::rlp::decodeHeader(cursor);
    if (error || !header.isList || header.payloadLength > cursor.size()) [[unlikely]]
    {
        return malformed();
    }
    bcos::bytesRef walker(cursor.data(), header.payloadLength);
    for (int i = 0; i < 6; ++i)
    {
        auto [fieldError, fieldHeader] = bcos::codec::rlp::decodeHeader(walker);
        if (fieldError || fieldHeader.payloadLength > walker.size()) [[unlikely]]
        {
            return malformed();
        }
        walker = walker.getCroppedData(fieldHeader.payloadLength);
    }
    if (walker.empty())
    {
        // pre-EIP-155 unprotected legacy (6-field preimage, v=27/28): no chainId, exempt —
        // op-geth HomesteadSigner.
        return unprotected();
    }
    // Peek fields 8/9 (without consuming field 7 yet) via the shared isLegacyPreimageTail
    // rule — the same rule Web3TxHandler decode and TransactionImpl reassemble apply, so
    // the three walk sites cannot classify the same trailer differently. field7Item keeps
    // the WHOLE field-7 item (header + payload): decodeHeader advances the walker to the
    // payload start, and decoding that payload as a fresh item mis-reads any multi-byte
    // chainId/v (e.g. chainId 8453 -> 33) or classifies it as a list header (chainId 200 ->
    // Unprotected exemption). Decode from the item start.
    bcos::bytesRef field7Item = walker;
    auto [field7Error, field7Header] = bcos::codec::rlp::decodeHeader(walker);
    if (field7Error || field7Header.payloadLength > walker.size()) [[unlikely]]
    {
        return malformed();
    }
    bcos::bytesRef afterField7 = walker.getCroppedData(field7Header.payloadLength);
    bool const isPreimageTail = [&] {
        if (afterField7.empty()) [[unlikely]]
        {
            return false;  // no 0,0 placeholders — treat as full form (or malformed)
        }
        auto [field8Error, field8Header] = bcos::codec::rlp::decodeHeader(afterField7);
        if (field8Error != nullptr || field8Header.isList || field8Header.payloadLength != 0)
        {
            return false;
        }
        if (afterField7.empty()) [[unlikely]]
        {
            return false;
        }
        auto [field9Error, field9Header] = bcos::codec::rlp::decodeHeader(afterField7);
        return field9Error == nullptr && !field9Header.isList && field9Header.payloadLength == 0 &&
               isLegacyPreimageTail(0, true, true);
    }();
    if (isPreimageTail)
    {
        // preimage form: field 7 is the EIP-155 chainId. Canonical form enforced (finding N).
        uint64_t chainId = 0;
        if (auto e = decodeCanonicalRlpUint(field7Item, chainId); e != nullptr) [[unlikely]]
        {
            return malformed();
        }
        return protectedChainId(chainId);
    }
    // Full envelope: field 7 is v. 27/28 = pre-EIP-155 unprotected (exempt); >= 35 = EIP-155
    // protected, chainId = (v - 35) >> 1. Anything else (0/1, 29-34) is malformed — fail
    // closed rather than folding it into the unprotected exemption.
    uint64_t v = 0;
    if (auto e = decodeCanonicalRlpUint(field7Item, v); e != nullptr) [[unlikely]]
    {
        return malformed();
    }
    if (v == 27 || v == 28)
    {
        return unprotected();
    }
    if (v >= 35)
    {
        return protectedChainId((v - 35) >> 1);
    }
    return malformed();
}

std::optional<uint64_t> web3ChainIdFromEnvelope(bcos::bytesConstRef payload)
{
    auto const result = classifyWeb3EnvelopeChainId(payload);
    if (result.kind != Web3EnvelopeChainIdKind::Protected)
    {
        // nullopt contract preserved: unprotected legacy AND malformed both collapse here
        // (the header documents this; chainId gates that need fail-closed use the classifier).
        return std::nullopt;
    }
    return result.chainId;
}
}  // namespace bcos::rlp::protocol
