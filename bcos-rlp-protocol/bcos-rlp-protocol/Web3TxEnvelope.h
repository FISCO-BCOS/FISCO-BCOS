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
/// unauthenticated tars mirror. Defined in the rlp-protocol library TU — callers must link
/// that target. (A previous comment claimed linking it broke libc++ typed catch via
/// wedprcrypto; that was measured false on the opstack-executor receipt suite, which already
/// links bcos-crypto / ledger / protocol-tars.)
///   typed (first byte < 0x80, not 0x7E): chainId = RLP field 0 of the inner list;
///   legacy: walk the first 6 fields; if a 7th is present it is the EIP-155 chainId or v.
/// nullopt = pre-EIP-155 unprotected legacy (6-field, v=27/28) or a malformed preimage.
[[nodiscard]] std::optional<uint64_t> web3ChainIdFromEnvelope(bcos::bytesConstRef payload);
}  // namespace bcos::rlp::protocol
