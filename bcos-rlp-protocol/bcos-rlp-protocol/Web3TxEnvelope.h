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

/// Decode an RLP unsigned integer REJECTING non-canonical encodings (#5496 finding N):
/// leading-zero payloads ("0x80 00" for 0), bare zero bytes (zero must be the empty string
/// 0x80), and oversized length prefixes ("0x81 05" for 5). geth's strict rlp decoder rejects
/// these everywhere; equality against the value's own minimal encoding pins every case at
/// once, so walk sites cannot drift on which corner they enforce.
template <typename T>
[[nodiscard]] inline bcos::Error::UniquePtr decodeCanonicalRlpUint(
    bcos::bytesRef& from, T& to) noexcept
{
    auto const* const start = from.data();
    if (auto error = bcos::codec::rlp::decode(from, to); error != nullptr)
    {
        return error;
    }
    bcos::bytes encoded;
    bcos::codec::rlp::encode(encoded, to);
    if (encoded.size() != static_cast<size_t>(from.data() - start) ||
        std::memcmp(start, encoded.data(), encoded.size()) != 0)
    {
        // NonCanonicalSize, not UnexpectedLength: the payload is in-range but spelled
        // non-minimally (#5496 finding AN/N family).
        return BCOS_ERROR_UNIQUE_PTR(
            bcos::codec::rlp::DecodingError::NonCanonicalSize, "non-canonical RLP integer");
    }
    return nullptr;
}

/// Typed-transaction yParity in STRICT wire form (#5496 finding M): the whole item (header
/// byte included) must be exactly 0x80 (parity 0) or 0x01 (parity 1). Anything else — the
/// bare 0x00 non-minimal zero, any other single byte, multi-byte payloads — is rejected,
/// matching strict references (op-geth rlp.Uint). Operating on the WHOLE item span avoids
/// depending on how this codec's header parser normalizes sub-0x80 inline items.
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

/// Stream-form twin of canonicalTypedYParityItem: parses ONE whole yParity item from the
/// cursor with identical strictness and consumes it on success (#5496 finding M). Both RLP
/// spellings of the legal values arrive as a 1-byte whole item — 0x01 inline for parity 1,
/// 0x80 (empty-string header) for parity 0 — because decodeHeader leaves sub-0x80 items
/// uncropped while advancing past string headers. Anything else (the bare 0x00 non-minimal
/// zero, other values, length-prefixed/list shapes) is rejected.
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


/// Classify a legacy 3-item trailer as the EIP-155 signing preimage (chainId, 0, 0) or a
/// sealed wire envelope (v, r, s). SINGLE HOME for the three walk sites — Web3TxHandler's
/// decode, TransactionImpl's reassemble, classifyWeb3EnvelopeChainId. RLP encodes integer zero as
/// an empty payload while valid secp256k1 r/s are never empty. A `(27|28, 0, 0)` trailer is
/// necessarily treated as an EIP-155 preimage: chain IDs 27 and 28 are valid, and the stored
/// bytes alone cannot distinguish those preimages from an invalid Homestead envelope whose
/// signature scalars were erased.
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

/// Three-way-plus-one classification of an envelope's chainId binding:
///   Unprotected — pre-EIP-155 legacy (6-field preimage, or full envelope v=27/28): exempt
///                 from the chainId gate (op-geth HomesteadSigner).
///   Protected   — chainId recovered from the envelope (typed field 0, EIP-155 v>=35, or the
///                 preimage form's field 7).
///   Malformed   — a legacy envelope whose tail is neither a valid unprotected form nor a
///                 recoverable protected one (e.g. v in {0,1} or [29,34], or an unparseable
///                 tail). A chainId gate that accepts these as "unprotected" would execute a
///                 transaction whose signature op-geth would reject — the exemption must be
///                 fail-closed.
///   Deposit     — the 0x7E deposit envelope: structurally chainId-less (field 0 is
///                 sourceHash). Its OWN kind so every gate keys an explicit policy on it
///                 instead of overloading Malformed and re-deriving "deposit vs junk" from
///                 first bytes (#5496 K). Policy: PUBLIC admission (txpool validateChainId,
///                 sendRawTransaction) rejects — legitimate deposits are injected by the
///                 rollup pipeline straight into block building, and a pool/RPC entry point
///                 would let a peer forge sourceHash/mint fields; the executor's chainId gate
///                 fail-closes too (executeDeposit bypasses it before dispatch).
/// `chainId` is meaningful only when the kind is Protected. Defined in the rlp-protocol TU.
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

/// Stable names for WARN logs at the three gate sites — spelled out rather than magic_enum
/// so this lightweight header stays free of the table dependency.
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
