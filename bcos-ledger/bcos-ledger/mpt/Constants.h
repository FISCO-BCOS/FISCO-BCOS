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
 * @brief Ethereum well-known hash constants for the MPT module (spec §5.5)
 */
#pragma once

#include "Types.h"

namespace bcos::ledger::mpt
{

/// Hex string for keccak256(RLP("")) = the empty trie root hash
constexpr std::string_view EMPTY_ROOT_HASH_HEX =
    "56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421";

/// Hex string for keccak256("") = the empty code hash
constexpr std::string_view EMPTY_CODE_HASH_HEX =
    "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470";

/// Returns the singleton Hash32 for the empty trie root.
/// Initialised from EMPTY_ROOT_HASH_HEX on first call (Meyers singleton).
Hash32 const& emptyRootHash();

/// Returns the singleton Hash32 for keccak256("") (empty code hash).
/// Initialised from EMPTY_CODE_HASH_HEX on first call (Meyers singleton).
Hash32 const& emptyCodeHash();

}  // namespace bcos::ledger::mpt
