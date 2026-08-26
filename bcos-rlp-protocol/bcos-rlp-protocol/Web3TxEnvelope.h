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
#include <cstdint>
#include <optional>

namespace bcos::rlp::protocol
{
/// True if the envelope's first byte is a typed-transaction marker (EIP-2718: type byte < 0x80).
/// Used to key typed/legacy decisions on the envelope rather than the forgeable mirror kind.
[[nodiscard]] inline bool isTypedWeb3Envelope(bcos::bytesConstRef payload) noexcept
{
    return !payload.empty() && payload[0] > 0 && payload[0] < bcos::codec::rlp::BYTES_HEAD_BASE;
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

/// Three-way classification of an envelope's chainId binding:
///   Unprotected — pre-EIP-155 legacy (6-field preimage, or full envelope v=27/28): exempt
///                 from the chainId gate (op-geth HomesteadSigner).
///   Protected   — chainId recovered from the envelope (typed field 0, EIP-155 v>=35, or the
///                 preimage form's field 7).
///   Malformed   — a legacy envelope whose tail is neither a valid unprotected form nor a
///                 recoverable protected one (e.g. v in {0,1} or [29,34], or an unparseable
///                 tail). A chainId gate that accepts these as "unprotected" would execute a
///                 transaction whose signature op-geth would reject — the exemption must be
///                 fail-closed.
/// `chainId` is meaningful only when the kind is Protected. Defined in the rlp-protocol TU.
enum class Web3EnvelopeChainIdKind : uint8_t
{
    Unprotected,
    Protected,
    Malformed,
};

struct Web3EnvelopeChainIdResult
{
    Web3EnvelopeChainIdKind kind;
    uint64_t chainId = 0;
};

[[nodiscard]] Web3EnvelopeChainIdResult classifyWeb3EnvelopeChainId(bcos::bytesConstRef payload);
}  // namespace bcos::rlp::protocol
