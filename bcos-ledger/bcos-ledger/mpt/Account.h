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
 * @file Account.h
 * @brief Ethereum account 4-tuple (nonce, balance, storageRoot, codeHash) with RLP
 *        encode/decode (spec §5.4)
 */
#pragma once

#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>

namespace bcos::ledger::mpt
{

/// The Ethereum account record stored at each leaf of the state trie.
/// RLP-encoded as the 4-element list [nonce, balance, storageRoot, codeHash]
/// (Yellow Paper §4.1). nonce/balance are big-endian-trimmed integers; the two
/// hashes are fixed 32-byte strings.
struct Account
{
    bcos::u256 nonce{};
    bcos::u256 balance{};
    bcos::h256 storageRoot;  ///< default-constructed to emptyRootHash()
    bcos::h256 codeHash;     ///< default-constructed to emptyCodeHash()

    /// Default account: nonce=0, balance=0, storageRoot=emptyRootHash(),
    /// codeHash=emptyCodeHash(). Matches a freshly created EOA with no code.
    Account();

    /// RLP-encode as list [nonce, balance, storageRoot, codeHash].
    [[nodiscard]] bcos::bytes encode() const;

    /// Inverse of encode(). Throws MPTDecodeError on a malformed input: not a
    /// list, wrong field count, trailing bytes, or any field-level RLP error.
    static Account decode(bcos::bytesConstRef rlp);
};

}  // namespace bcos::ledger::mpt
