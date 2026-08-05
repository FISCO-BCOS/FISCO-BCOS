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
 * @file OpHeaderCodec.h
 * @brief ETH/OP execution-layer block header: 21-field RLP + keccak block hash, driven by the
 *        FISCO `protocol::BlockHeader` interface. Replaces the retired `EthBlockHeader` struct:
 *        18 tars-carried fields are read via the header accessors; the 3 post-merge protocol
 *        constants (ommersHash/difficulty/nonce) have no tars carrier and are injected via
 *        `OpHeaderConst` (spec 2026-08-05-opstack-blockheader-fisco-adaptation-design.md §11 D5).
 * @date 2026-08-05
 */
#pragma once
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/Error.h"
#include "bcos-utilities/FixedBytes.h"
#include <cstdint>

namespace bcos::codec::rlp
{
/// 3 post-merge protocol constants with no tars carrier (spec §11 D5). The caller supplies them —
/// this codec never hardcodes a value.
struct OpHeaderConst
{
    bcos::h256 ommersHash;  // keccak256(rlp([])) on post-merge/OP chains
    bcos::u256 difficulty;  // 0 on post-merge/OP chains
    bcos::h64 nonce;        // 8 zero bytes on post-merge/OP chains
};

/// 21-field RLP, field order identical to the retired `EthBlockHeader::encode()` (pinned
/// byte-for-byte by the golden gate in bcos-evm/test/opstack/EthBlockHeaderTest.cpp). Reads the
/// header timestamp as MILLISECONDS (FISCO convention) and encodes SECONDS (OP convention) — the
/// /1000 is what keeps the bytes identical to the golden corpus (spec §7).
[[nodiscard]] bcos::bytes encodeOpHeader(
    const bcos::protocol::BlockHeader& h, const OpHeaderConst& c);

/// keccak256(encodeOpHeader(h, c)) — the ETH/OP block hash.
[[nodiscard]] bcos::h256 opHeaderHash(const bcos::protocol::BlockHeader& h, const OpHeaderConst& c);

/// Inverse of encodeOpHeader(): parses the 21-field RLP into `h` (the 18 tars-carried fields via
/// the accessors) and the 3 constants into `c`. `in` must contain exactly one header list —
/// trailing bytes are an error, not ignored. Stricter than RLPDecode.h's generic overloads on
/// purpose: fixed-width fields must arrive at EXACTLY their width and scalars must be canonical
/// (no leading zeros) — a header that does not round-trip byte-for-byte would hash to something
/// other than the block hash it was stored under.
[[nodiscard]] bcos::Error::UniquePtr decodeOpHeader(
    bcos::bytesRef in, bcos::protocol::BlockHeader& h, OpHeaderConst& c);
}  // namespace bcos::codec::rlp
