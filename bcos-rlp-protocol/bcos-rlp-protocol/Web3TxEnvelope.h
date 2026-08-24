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
#include <optional>

namespace bcos::rlp::protocol
{
/// Chain id from a Web3 transaction's SIGNED envelope (extraTransactionBytes), never the
/// unauthenticated tars mirror. The signature binds only the envelope bytes, so a mirror field
/// is forgeable by a malicious peer/proposer; the envelope is authoritative.
///   typed (first byte < 0x80): chainId = RLP field 0 of the inner list;
///   legacy: walk the first 6 fields; if a 7th is present it is the EIP-155 chainId.
/// nullopt = pre-EIP-155 unprotected legacy (6-field, v=27/28, no chainId tail) or a malformed
/// preimage. A malformed tail is normally rejected upstream by reassembleWeb3RawTransaction /
/// verify() — keep the walkers' strictness in sync if that ordering ever changes.
/// @param payload the signed envelope bytes (type byte included for typed txs)
[[nodiscard]] std::optional<uint64_t> web3ChainIdFromEnvelope(bcos::bytesConstRef payload);

/// True if the envelope's first byte is a typed-transaction marker (EIP-2718: type byte < 0x80).
/// Used to key typed/legacy decisions on the envelope rather than the forgeable mirror kind.
[[nodiscard]] inline bool isTypedWeb3Envelope(bcos::bytesConstRef payload) noexcept
{
    return !payload.empty() && payload[0] > 0 && payload[0] < bcos::codec::rlp::BYTES_HEAD_BASE;
}
}  // namespace bcos::rlp::protocol
