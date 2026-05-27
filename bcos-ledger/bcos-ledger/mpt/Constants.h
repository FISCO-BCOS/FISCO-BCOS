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

/// Hex string for keccak256(RLP("")) = the empty trie root hash
inline constexpr std::string_view EMPTY_ROOT_HASH_HEX =
    "56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421";

/// Hex string for keccak256("") = the empty code hash
inline constexpr std::string_view EMPTY_CODE_HASH_HEX =
    "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470";

/// Returns the singleton h256 for the empty trie root.
/// Initialised from EMPTY_ROOT_HASH_HEX on first call (Meyers singleton).
bcos::h256 const& emptyRootHash();

/// Returns the singleton h256 for keccak256("") (empty code hash).
/// Initialised from EMPTY_CODE_HASH_HEX on first call (Meyers singleton).
bcos::h256 const& emptyCodeHash();

}  // namespace bcos::ledger::mpt
