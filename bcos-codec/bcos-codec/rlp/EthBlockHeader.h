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
 * @file EthBlockHeader.h
 * @brief ETH/OP execution-layer block header: 21-field RLP encoding + keccak block hash.
 *        Field set/order follows go-ethereum's core/types.Header (Cancun+ superset), per
 *        docs/superpowers/specs/2026-07-28-op-validator-minimal-loop-design.md §5.1.
 * @date 2026-07-28
 */

#pragma once
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <cstdint>

namespace bcos::codec::rlp
{

// 21-field ETH/OP block header. Every field is a plain public member so the caller (gate/test
// fixture) can seed it from whatever source is appropriate (payload, golden table, protocol
// constant) — this struct only knows how to encode/hash, not where values come from.
struct EthBlockHeader
{
    bcos::h256 parentHash;
    // Post-merge/OP chains have no uncles: this is always keccak256(rlp([])) in practice, but
    // it is a real field here (not hardcoded into encode()) rather than an assumed constant.
    bcos::h256 ommersHash;
    bcos::Address feeRecipient;
    bcos::h256 stateRoot;
    bcos::h256 transactionsRoot;
    bcos::h256 receiptsRoot;
    bcos::h2048 logsBloom;
    // Post-merge/PoS chains: always 0. Real field, not hardcoded.
    bcos::u256 difficulty;
    uint64_t number{0};
    uint64_t gasLimit{0};
    uint64_t gasUsed{0};
    uint64_t timestamp{0};
    // Holocene+ EIP-1559 parameters, emitted verbatim from the source of truth (never
    // hand-picked — spec §5.1 rev.2/rev.3 note): Isthmus is 9 bytes = 0x00 (version) ‖
    // denominator (uint32 BE) ‖ elasticity (uint32 BE); Jovian is 17 bytes = 0x01 (version —
    // op-geth's own encoder bumps it to signal the appended field, confirmed empirically
    // against golden, not assumed) ‖ denominator ‖ elasticity ‖ minBaseFee (uint64 BE).
    bcos::bytes extraData;
    bcos::h256 prevRandao;
    // Post-merge/PoS chains: always 8 zero bytes. Real field, not hardcoded.
    bcos::h64 nonce;
    bcos::u256 baseFeePerGas;
    bcos::h256 withdrawalsRoot;
    // Isthmus: must be 0 (that invariant is the caller's business, not this struct's — spec
    // §5.1 note). Jovian+: repurposed as the DA footprint (spec §5.1 note / OpBlockSeal.h);
    // this struct only carries and encodes whatever value it is given.
    uint64_t blobGasUsed{0};
    uint64_t excessBlobGas{0};
    bcos::h256 parentBeaconBlockRoot;
    // = sha256("") in this corpus (spec §5.1 note, OP_EMPTY_REQUESTS_HASH), but stored/encoded
    // as a plain field rather than hardcoded so a genuine non-empty requestsHash still works.
    bcos::h256 requestsHash;

    // RLP-encodes all 21 fields above, in the fixed order declared, as a single top-level RLP
    // list. Field-level assertion against golden.encodedHeaderHex must precede the hash()
    // assertion (spec §7.5, decision C3) — encode() is the primary correctness surface, hash()
    // is derived from it.
    [[nodiscard]] bcos::bytes encode() const;
    // keccak256(encode()) — the ETH/OP block hash.
    [[nodiscard]] bcos::h256 hash() const;
};

}  // namespace bcos::codec::rlp
