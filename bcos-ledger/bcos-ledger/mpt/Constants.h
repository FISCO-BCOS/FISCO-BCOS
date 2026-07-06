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
 * @file Constants.h
 * @brief Ethereum well-known constants for the MPT module (spec §5.5)
 */
#pragma once

// AnyHasher.h supplies the Hasher concept and the free bcos::crypto::hasher::hash();
// OpenSSLHasher.h supplies the keccak256 default. Both named directly below.
#include <bcos-crypto/hasher/AnyHasher.h>
#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <string_view>

namespace bcos::ledger::mpt
{

/// RLP encoding of the empty byte-string (Yellow Paper §B item-encoding):
/// a zero-length string encodes to the single byte 0x80. In the MPT this byte
/// is reused as:
///   - the complete RLP form of EmptyNode
///   - an absent-child placeholder inside a BranchNode payload
inline constexpr bcos::byte RLP_EMPTY_STRING = 0x80;

/// RLP header byte for a 32-byte string (RLP_EMPTY_STRING | 32 == 0xa0): the one-byte prefix that
/// precedes a node's keccak256 digest when a hash node-reference is spliced into a parent payload.
inline constexpr bcos::byte RLP_HASH_REF_PREFIX = 0xa0;

/// Encoded size of a hash node-reference: the 1-byte RLP_HASH_REF_PREFIX plus the 32-byte digest.
inline constexpr size_t HASH_REF_ENCODED_SIZE = 1 + bcos::h256::SIZE;

/// Hex string for keccak256(RLP("")) = the empty trie root hash under the DEFAULT hasher.
/// Kept as the pinned literal the keccak instantiation of emptyRootHash() must reproduce
/// (asserted in ConstantsTest — the oracle for the computed-on-first-use value below).
inline constexpr std::string_view EMPTY_ROOT_HASH_HEX =
    "56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421";

/// Hex string for keccak256("") = the empty code hash under the DEFAULT hasher.
inline constexpr std::string_view EMPTY_CODE_HASH_HEX =
    "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470";

/// The empty trie root under @tparam HasherT: HasherT(RLP("")) = HasherT({0x80}). keccak256 by
/// default; an SM3 instantiation yields the guomi chain's empty root. Computed on first use and
/// cached per hasher type (one Meyers singleton per instantiation) — computing from the
/// definition instead of hardcoding per-algorithm literals makes every instantiation correct by
/// construction; the keccak one is pinned against EMPTY_ROOT_HASH_HEX in ConstantsTest.
template <
    bcos::crypto::hasher::Hasher HasherT = bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher>
bcos::h256 const& emptyRootHash()
{
    static bcos::h256 const s_value = [] {
        HasherT hasher;
        bcos::h256 out;
        auto const rlpEmptyString = RLP_EMPTY_STRING;
        bcos::crypto::hasher::hash(
            hasher, bcos::bytesConstRef(&rlpEmptyString, sizeof(rlpEmptyString)), out);
        return out;
    }();
    return s_value;
}

/// The empty code hash under @tparam HasherT: HasherT("") (zero-length input). Same
/// compute-on-first-use scheme as emptyRootHash().
template <
    bcos::crypto::hasher::Hasher HasherT = bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher>
bcos::h256 const& emptyCodeHash()
{
    static bcos::h256 const s_value = [] {
        HasherT hasher;
        bcos::h256 out;
        // A zero-length view over valid storage (not a null pointer) keeps EVP_DigestUpdate
        // strictly in-contract.
        auto const anchor = RLP_EMPTY_STRING;
        bcos::crypto::hasher::hash(hasher, bcos::bytesConstRef(&anchor, 0), out);
        return out;
    }();
    return s_value;
}

}  // namespace bcos::ledger::mpt
