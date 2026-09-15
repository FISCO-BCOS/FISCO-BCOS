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
[[nodiscard]] bool isTypedWeb3Envelope(bcos::bytesConstRef payload) noexcept;

/// Decode an RLP unsigned integer, rejecting non-minimal encodings (leading zeros, bare 0x00,
/// oversized prefixes) with NonCanonicalSize. Since #5353 the shared decoder enforces all of
/// that itself (RLPDecode.h: width gate, header canonicality, leading-zero payload), so this is
/// a plain forward kept for its name at the ~60 Web3TxHandler call sites. Throws
/// codec::rlp::RlpDecodeException on malformed input.
/// (Template — must stay in the header.)
template <typename T>
inline void decodeCanonicalRlpUint(bcos::bytesRef& from, T& to)
{
    bcos::codec::rlp::decode(from, to);
}

/// Variadic sibling of decodeCanonicalRlpUint: decode several canonical RLP integers in order.
/// The fold expression keeps the exact per-field sequencing (and error behaviour) of the
/// scalar overload. Throws codec::rlp::RlpDecodeException on malformed input.
/// (Template — must stay in the header.)
template <typename... Ts>
inline void decodeCanonicalRlpUints(bcos::bytesRef& from, Ts&... tos)
{
    (decodeCanonicalRlpUint(from, tos), ...);
}

/// Typed yParity: whole item must be 0x80 (0) or 0x01 (1). Bare 0x00 is rejected.
[[nodiscard]] std::optional<uint64_t> canonicalTypedYParityItem(bcos::bytesConstRef item) noexcept;

/// Consume one canonical yParity item (0x80 / 0x01) from the cursor.
/// Throws codec::rlp::RlpDecodeException on malformed input.
void decodeCanonicalYParity(bcos::bytesRef& from, uint64_t& to);

/// Consume one EIP-7702 authorization yParity item the way op-geth's RLP decoder reads a
/// uint8: an empty byte string is 0, a single non-zero payload byte is its value, and a
/// leading-zero or multi-byte payload is rejected as non-canonical/overflowing. The admitted
/// wire forms are the inline single byte 0x01..0x7f and the 0x81 XX form with XX >= 0x80; the
/// shared canonical-RLP decoder (decodeHeader) rejects 0x81 with a payload below 0x80 as
/// NonCanonicalSize before this function sees it — stricter than op-geth, which accepts it.
///
/// Values above 1 are LEGAL here, unlike the transaction-signature domain above: op-geth
/// decodes the authorization's V as a plain uint8 and skips the entry at execution when it is
/// not 0/1, so the transaction stays valid. Rejecting it at decode would reject a whole block
/// op-geth accepts (minus the bad entry).
///
/// Throws codec::rlp::RlpDecodeException on malformed input.
void decodeAuthorizationYParity(bcos::bytesRef& from, uint64_t& to);

/// Empty r/s => EIP-155 preimage (chainId, 0, 0); otherwise sealed (v, r, s).
/// chainId 27/28 is indistinguishable from an erased Homestead signature.
/// (constexpr — must stay in the header.)
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
[[nodiscard]] std::string_view toString(Web3EnvelopeChainIdKind kind) noexcept;

[[nodiscard]] Web3EnvelopeChainIdResult classifyWeb3EnvelopeChainId(bcos::bytesConstRef payload);
}  // namespace bcos::rlp::protocol
