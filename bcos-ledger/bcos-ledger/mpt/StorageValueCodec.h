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
 * @file StorageValueCodec.h
 * @brief Storage-slot key transform and value encoding for the secure trie (spec §5.3)
 */
#pragma once

#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>

namespace bcos::ledger::mpt
{

/// The secure-trie key transform for storage slots: keccak256 of the 32 raw slot-key bytes.
/// The counterpart of accountKeyHash() one trie level up; every producer of storage-trie paths
/// (genesis build, MPTBuilder, proof generation) must apply this identical transform.
bcos::h256 slotKeyHash(bcos::h256 const& slot);

/// Ethereum storage-value leaf encoding: RLP of the value with leading zero bytes trimmed
/// (the minimal big-endian byte string). Returns an EMPTY buffer when the value trims to
/// nothing — a zero value is not part of the storage trie, so an empty result means "delete
/// the slot" to the caller. (A present zero-length value still RLP-encodes to 0x80, one byte,
/// so the empty return is unambiguous.)
bcos::bytes encodeStorageValue(bcos::bytesConstRef value);

}  // namespace bcos::ledger::mpt
