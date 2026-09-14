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
 * @brief Library TU for the Web3TxEnvelope walkers. Kept out of the public header so every
 *        consumer compiles one copy (link-period identity) rather than one copy per include.
 */
#include "Web3TxEnvelope.h"
#include <bcos-codec/rlp/RLPDecode.h>

namespace bcos::rlp::protocol
{
bool isTypedWeb3Envelope(bcos::bytesConstRef payload) noexcept
{
    return !payload.empty() && payload[0] > 0 && payload[0] < bcos::codec::rlp::BYTES_HEAD_BASE;
}

std::optional<uint64_t> canonicalTypedYParityItem(bcos::bytesConstRef item) noexcept
{
    if (item.size() == 1 && item[0] == 0x80)
    {
        return uint64_t{0};
    }
    if (item.size() == 1 && item[0] == 0x01)
    {
        return uint64_t{1};
    }
    return std::nullopt;
}

void decodeCanonicalYParity(bcos::bytesRef& from, uint64_t& to)
{
    auto const* const start = from.data();
    auto header = bcos::codec::rlp::decodeHeader(from);
    if (header.isList)
    {
        bcos::codec::rlp::throwRlpDecodeError(
            bcos::codec::rlp::DecodingError::UnexpectedList, "y_parity: expected a scalar");
    }
    auto const itemLength = static_cast<size_t>(from.data() - start) + header.payloadLength;
    auto const parity = canonicalTypedYParityItem({start, itemLength});
    if (!parity.has_value()) [[unlikely]]
    {
        bcos::codec::rlp::throwRlpDecodeError(bcos::codec::rlp::DecodingError::InvalidVInSignature,
            "typed tx y_parity must be the canonical 0x80/0x01 form");
    }
    to = *parity;
    from = from.getCroppedData(header.payloadLength);
}

void decodeAuthorizationYParity(bcos::bytesRef& from, uint64_t& to)
{
    auto header = bcos::codec::rlp::decodeHeader(from);
    if (header.isList)
    {
        bcos::codec::rlp::throwRlpDecodeError(
            bcos::codec::rlp::DecodingError::UnexpectedList, "y_parity: expected a scalar");
    }
    uint64_t value = 0;
    if (header.payloadLength > 1)
    {
        bcos::codec::rlp::throwRlpDecodeError(bcos::codec::rlp::DecodingError::InvalidVInSignature,
            "authorization y_parity must be a canonical uint8");
    }
    if (header.payloadLength == 1)
    {
        if (from.data()[0] == 0)
        {
            bcos::codec::rlp::throwRlpDecodeError(
                bcos::codec::rlp::DecodingError::InvalidVInSignature,
                "authorization y_parity has a leading zero byte");
        }
        value = from.data()[0];
    }
    to = value;
    from = from.getCroppedData(header.payloadLength);
}

std::string_view toString(Web3EnvelopeChainIdKind kind) noexcept
{
    switch (kind)
    {
    case Web3EnvelopeChainIdKind::Unprotected:
        return "Unprotected";
    case Web3EnvelopeChainIdKind::Protected:
        return "Protected";
    case Web3EnvelopeChainIdKind::Malformed:
        return "Malformed";
    case Web3EnvelopeChainIdKind::Deposit:
        return "Deposit";
    }
    return "Unknown";
}

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
    // tryDecodeHeader/tryDecode require a mutable bytesRef cursor even for read-only parsing;
    // the cast is safe because this function never writes through the cursor.
    bcos::bytesRef cursor(const_cast<bcos::byte*>(payload.data()), payload.size());
    // 0x7E deposit: field 0 is sourceHash, not chainId.
    if (firstByte == 0x7E) [[unlikely]]
    {
        return {.kind = Web3EnvelopeChainIdKind::Deposit};
    }
    // Every RLP failure below surfaces as Malformed (the caller maps it to a reject); the
    // non-throwing tryDecodeHeader/tryDecode cores keep this hot admission path free of
    // exception unwinds on attacker-controlled input.
    auto const takeHeader = [](bcos::bytesRef& view, bcos::codec::rlp::Header& header) {
        auto result = bcos::codec::rlp::tryDecodeHeader(view);
        if (!result) [[unlikely]]
        {
            return false;
        }
        header = *result;
        return true;
    };
    if (firstByte > 0 && firstByte < bcos::codec::rlp::BYTES_HEAD_BASE)
    {
        // Typed: chainId is inner-list field 0. Non-minimal RLP is Malformed.
        cursor = cursor.getCroppedData(1);
        bcos::codec::rlp::Header header{};
        if (!takeHeader(cursor, header)) [[unlikely]]
        {
            return malformed();
        }
        if (!header.isList) [[unlikely]]
        {
            return malformed();
        }
        bcos::bytesRef listPayload = cursor.getCroppedData(0, header.payloadLength);
        cursor = cursor.getCroppedData(header.payloadLength);
        uint64_t chainId = 0;
        if (!bcos::codec::rlp::tryDecode(listPayload, chainId)) [[unlikely]]
        {
            return malformed();
        }
        // Finding S6: the inner list must close at its declared payload boundary and no
        // bytes may follow the list — decode() rejects post-list junk and overrunning
        // items, so the classifier must see the same bytes as invalid (same rationale as
        // the legacy ListEnd gate below).
        while (!listPayload.empty())
        {
            bcos::codec::rlp::Header tailHeader{};
            if (!takeHeader(listPayload, tailHeader)) [[unlikely]]
            {
                return malformed();
            }
            listPayload = listPayload.getCroppedData(tailHeader.payloadLength);
        }
        if (!cursor.empty()) [[unlikely]]
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
    // In the preimage form field 7 IS the chainId and fields 8/9 are the empty 0,0
    // placeholders; in the full form field 7 is v (27/28 unprotected, or chainId*2+35+yParity)
    // and fields 8/9 are the non-empty r/s scalars (EIP-2 keeps r,s in [1,n-1], never empty).
    // Distinguish by whether field 8 is an empty byte string.
    bcos::codec::rlp::Header header{};
    if (!takeHeader(cursor, header)) [[unlikely]]
    {
        return malformed();
    }
    if (!header.isList) [[unlikely]]
    {
        return malformed();
    }
    // The outer list must be the LAST thing in the envelope — the typed arm rejects the
    // same shape with `!cursor.empty()` (post-list junk), and reassembleWeb3RawTransaction
    // rejects it too; a legacy envelope with bytes after the list would otherwise classify
    // Protected and pass admission gates that decode() later refuses (poisoning window).
    if (cursor.size() != header.payloadLength) [[unlikely]]
    {
        return malformed();
    }
    bcos::bytesRef walker(cursor.data(), header.payloadLength);
    for (int i = 0; i < 6; ++i)
    {
        bcos::codec::rlp::Header fieldHeader{};
        if (!takeHeader(walker, fieldHeader)) [[unlikely]]
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
    // Peek r/s emptiness before consuming field 7. Decode field 7 from the item start
    // (header + payload); a payload-only decode misreads multi-byte chainId/v.
    bcos::bytesRef field7Item = walker;
    bcos::codec::rlp::Header field7Header{};
    if (!takeHeader(walker, field7Header)) [[unlikely]]
    {
        return malformed();
    }
    bcos::bytesRef afterField7 = walker.getCroppedData(field7Header.payloadLength);
    // ListEnd parity: both 9-item spellings — full form (v, r, s) and preimage tail
    // (chainId, 0, 0) — must END at the list boundary. Web3TxHandler's decode rejects
    // junk after the envelope, so the classifier must see the same bytes as invalid;
    // otherwise a junk-tailed envelope passes classifier-based admission gates it would
    // later fail in decode (poisoning window). Fail closed on any other tail shape.
    {
        bcos::bytesRef tail = afterField7;
        int tailItems = 0;
        while (!tail.empty())
        {
            bcos::codec::rlp::Header tailHeader{};
            if (!takeHeader(tail, tailHeader)) [[unlikely]]
            {
                return malformed();
            }
            tail = tail.getCroppedData(tailHeader.payloadLength);
            ++tailItems;
        }
        if (tailItems != 2) [[unlikely]]
        {
            return malformed();
        }
    }
    // Probe the tail on a LOCAL copy: tryDecodeHeader advances its argument, and the
    // emptySeen walk below re-walks the same tail from the pristine afterField7 — a
    // shared probe cursor would leave that walk starting inside r/s payloads, yielding
    // data-dependent Malformed verdicts on real-width (32-byte) signatures (kyonRay R3 #1).
    // nullopt = RLP failure -> Malformed (previously the same failure threw and was
    // caught into Malformed).
    std::optional<bool> const isPreimageTail = [&]() -> std::optional<bool> {
        bcos::bytesRef tailProbe = afterField7;
        if (tailProbe.empty()) [[unlikely]]
        {
            return false;  // no 0,0 placeholders — treat as full form (or malformed)
        }
        // Emptiness is computed once as the full predicate and doubles as the guard, so
        // the values handed to the shared discriminator are the real decoded results —
        // never a tautology re-derived after a guard that already proved them (Codacy:
        // 'field9Header.payloadLength == 0 is always true').
        bcos::codec::rlp::Header field8Header{};
        if (!takeHeader(tailProbe, field8Header)) [[unlikely]]
        {
            return std::nullopt;
        }
        bool const field8Empty = !field8Header.isList && field8Header.payloadLength == 0;
        if (!field8Empty)
        {
            return false;
        }
        if (tailProbe.empty()) [[unlikely]]
        {
            return false;
        }
        bcos::codec::rlp::Header field9Header{};
        if (!takeHeader(tailProbe, field9Header)) [[unlikely]]
        {
            return std::nullopt;
        }
        bool const field9Empty = !field9Header.isList && field9Header.payloadLength == 0;
        if (!field9Empty)
        {
            return false;
        }
        uint64_t field7 = 0;
        bcos::bytesRef field7Cursor = field7Item;
        if (!bcos::codec::rlp::tryDecode(field7Cursor, field7)) [[unlikely]]
        {
            return std::nullopt;
        }
        return isLegacyPreimageTail(field7, field8Empty, field9Empty);
    }();
    if (!isPreimageTail.has_value()) [[unlikely]]
    {
        return malformed();
    }
    if (!*isPreimageTail)
    {
        // Finding S6: the full form must match what decode()'s sealed branch accepts —
        // EIP-2 keeps r,s in [1, n-1], so a sealed envelope never carries an empty r/s
        // item (only the preimage tail's 0,0 placeholders are empty). Exactly one empty
        // item classified Unprotected/Protected here but is rejected in decode — fail
        // closed instead. Anchored on the PRISTINE tail cursor (afterField7), never on a
        // cursor the preimage probe above moved.
        bcos::bytesRef tailCheck = afterField7;
        bool emptySeen = false;
        for (int i = 0; i < 2; ++i)
        {
            bcos::codec::rlp::Header itemHeader{};
            if (!takeHeader(tailCheck, itemHeader)) [[unlikely]]
            {
                return malformed();
            }
            if (itemHeader.isList) [[unlikely]]
            {
                return malformed();
            }
            if (itemHeader.payloadLength == 0)
            {
                emptySeen = true;
            }
            tailCheck = tailCheck.getCroppedData(itemHeader.payloadLength);
        }
        if (emptySeen) [[unlikely]]
        {
            return malformed();
        }
    }
    if (*isPreimageTail)
    {
        // Preimage: field 7 is the EIP-155 chainId.
        uint64_t chainId = 0;
        if (!bcos::codec::rlp::tryDecode(field7Item, chainId)) [[unlikely]]
        {
            return malformed();
        }
        return protectedChainId(chainId);
    }
    // Full envelope: field 7 is v. 27/28 = pre-EIP-155 unprotected (exempt); >= 35 = EIP-155
    // protected, chainId = (v - 35) >> 1. Anything else (0/1, 29-34) is malformed — fail
    // closed rather than folding it into the unprotected exemption.
    uint64_t v = 0;
    if (!bcos::codec::rlp::tryDecode(field7Item, v)) [[unlikely]]
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
