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
 * @file Web3TxEnvelope.h
 * @brief Signed-envelope walkers for Web3 transactions (chainId / typed-vs-legacy)
 * @date 2026/8/21
 */
#pragma once

#include "bcos-utilities/Common.h"
#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <cstdint>
#include <cstring>
#include <optional>

namespace bcos::rlp::protocol
{
/// True if the envelope's first byte is a typed-transaction marker (EIP-2718: type byte < 0x80).
/// Used to key typed/legacy decisions on the envelope rather than the forgeable mirror kind.
[[nodiscard]] inline bool isTypedWeb3Envelope(bcos::bytesConstRef payload) noexcept
{
    return !payload.empty() && payload[0] > 0 && payload[0] < bcos::codec::rlp::BYTES_HEAD_BASE;
}

/// Decode an RLP unsigned integer; reject non-minimal encodings (leading zeros, bare 0x00,
/// oversized prefixes). Re-encode must match the source bytes.
template <typename T>
[[nodiscard]] inline bcos::Error::UniquePtr decodeCanonicalRlpUint(
    bcos::bytesRef& from, T& to) noexcept
{
    auto const* const start = from.data();
    if (auto error = bcos::codec::rlp::decode(from, to); error != nullptr)
    {
        return error;
    }
    // Canonicality checked in place (finding BA: the decode-then-re-encode roundtrip cost
    // 1-2 heap allocations per scalar on the shared decode funnel). The consumed item
    // [start, from.data()) must be the minimal RLP spelling of an unsigned integer:
    //   0        -> exactly {0x80} (empty payload)
    //   1..0x7f  -> single inline byte ({0x00} would be the non-canonical spelling of 0)
    //   >=0x80   -> prefix 0x80+len + minimal big-endian payload: no leading zero, and a
    //              one-byte payload must be >= 0x80 or it should have been inline.
    // Payload width beyond T is already rejected by decode above.
    auto const consumed = static_cast<std::size_t>(from.data() - start);
    if (consumed == 1)
    {
        if (*start == 0x00) [[unlikely]]
        {
            return BCOS_ERROR_UNIQUE_PTR(
                bcos::codec::rlp::DecodingError::NonCanonicalSize, "non-canonical RLP integer");
        }
        return nullptr;
    }
    auto const headerByte = *start;
    auto const payloadLength = consumed - 1;
    if (headerByte < 0x80 || headerByte != static_cast<bcos::byte>(0x80 + payloadLength) ||
        start[1] == 0x00 || (payloadLength == 1 && start[1] < 0x80)) [[unlikely]]
    {
        // In-range but non-minimal spelling.
        return BCOS_ERROR_UNIQUE_PTR(
            bcos::codec::rlp::DecodingError::NonCanonicalSize, "non-canonical RLP integer");
    }
    return nullptr;
}

/// Typed yParity: whole item must be 0x80 (0) or 0x01 (1). Bare 0x00 is rejected.
[[nodiscard]] inline std::optional<uint64_t> canonicalTypedYParityItem(
    bcos::bytesConstRef item) noexcept
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

/// Consume one canonical yParity item (0x80 / 0x01) from the cursor.
[[nodiscard]] inline bcos::Error::UniquePtr decodeCanonicalYParity(
    bcos::bytesRef& from, uint64_t& to) noexcept
{
    auto const* const start = from.data();
    auto&& [error, header] = bcos::codec::rlp::decodeHeader(from);
    if (error != nullptr)
    {
        return std::move(error);
    }
    if (header.isList)
    {
        return BCOS_ERROR_UNIQUE_PTR(
            bcos::codec::rlp::DecodingError::UnexpectedList, "y_parity: expected a scalar");
    }
    auto const itemLength = static_cast<size_t>(from.data() - start) + header.payloadLength;
    auto const parity = canonicalTypedYParityItem({start, itemLength});
    if (!parity.has_value()) [[unlikely]]
    {
        return BCOS_ERROR_UNIQUE_PTR(bcos::codec::rlp::DecodingError::InvalidVInSignature,
            "typed tx y_parity must be the canonical 0x80/0x01 form");
    }
    to = *parity;
    from = from.getCroppedData(header.payloadLength);
    return nullptr;
}


/// Empty r/s => EIP-155 preimage (chainId, 0, 0); otherwise sealed (v, r, s).
/// chainId 27/28 is indistinguishable from an erased Homestead signature.
[[nodiscard]] constexpr bool isLegacyPreimageTail(
    [[maybe_unused]] uint64_t field7, bool field8Empty, bool field9Empty) noexcept
{
    return field8Empty && field9Empty;
}

/// Chain id from a Web3 transaction's SIGNED envelope (extraTransactionBytes), never the
/// unauthenticated tars mirror. The signature binds only the envelope bytes, so a mirror field
/// is forgeable by a malicious peer/proposer; the envelope is authoritative.
/// Defined in the rlp-protocol library TU — callers must link that target.
///   typed (first byte < 0x80, not 0x7E): chainId = RLP field 0 of the inner list;
///   legacy: walk the first 6 fields; if a 7th is present it is the EIP-155 chainId or v.
/// nullopt = pre-EIP-155 unprotected legacy (6-field, v=27/28) or a malformed preimage.
/// A malformed tail is normally rejected upstream by reassembleWeb3RawTransaction /
/// verify() — keep the walkers' strictness in sync if that ordering ever changes.
[[nodiscard]] std::optional<uint64_t> web3ChainIdFromEnvelope(bcos::bytesConstRef payload);

/// Envelope chainId kind. chainId is set only for Protected.
///   Unprotected — pre-EIP-155 (6-field or v=27/28); gate-exempt
///   Protected   — typed field 0, EIP-155 v>=35, or preimage field 7
///   Malformed   — unreadable v/chainId; must not use the unprotected exemption
///   Deposit     — 0x7E, no chainId. Pool/RPC reject; executeDeposit skips the gate.
enum class Web3EnvelopeChainIdKind : uint8_t
{
    Unprotected,
    Protected,
    Malformed,
    Deposit,
};

struct Web3EnvelopeChainIdResult
{
    Web3EnvelopeChainIdKind kind;
    uint64_t chainId = 0;
};

/// Log names; spelled out so this header does not pull in magic_enum. The trailing
/// "Unknown" return is unreachable today (MSVC C4715 totality) and does NOT mask -Wswitch:
/// a kind added later fires that warning, since there is no default: label.
[[nodiscard]] inline std::string_view toString(Web3EnvelopeChainIdKind kind) noexcept
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

[[nodiscard]] Web3EnvelopeChainIdResult classifyWeb3EnvelopeChainId(bcos::bytesConstRef payload);
}  // namespace bcos::rlp::protocol
