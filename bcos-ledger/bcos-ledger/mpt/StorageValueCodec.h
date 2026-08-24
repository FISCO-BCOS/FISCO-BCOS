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

// AnyHasher.h supplies the Hasher concept and the free bcos::crypto::hasher::hash().
#include <bcos-crypto/hasher/AnyHasher.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>

namespace bcos::ledger::mpt
{

/// The secure-trie key transform for storage slots: hash of the 32 raw slot-key bytes
/// (keccak256 on Ethereum-compatible chains, SM3 on guomi ones — whatever @p hasher computes).
/// The counterpart of accountKeyHash() one trie level up; every producer of storage-trie paths
/// (genesis build, MPTBuilder, proof generation) must apply this identical transform.
///
/// Hasher-injection form (NodeEncoder's hot-path convention): the caller owns @p hasher and
/// reuses it across calls — MPTBuilder hashes one slot key per storage change, so a fresh
/// hash context per call would be a per-slot allocation.
template <bcos::crypto::hasher::Hasher HasherT>
bcos::h256 slotKeyHash(bcos::h256 const& slot, HasherT& hasher)
{
    bcos::h256 out;
    bcos::crypto::hasher::hash(hasher, slot.ref(), out);
    return out;
}

/// Convenience overload constructing a fresh keccak256 hasher per call. For one-off /
/// low-frequency use (tests, tooling); hot paths use the injection form above.
bcos::h256 slotKeyHash(bcos::h256 const& slot);

/// Ethereum storage-value leaf encoding: RLP of the value with leading zero bytes trimmed
/// (the minimal big-endian byte string). Returns an EMPTY buffer when the value trims to
/// nothing — a zero value is not part of the storage trie, so an empty result means "delete
/// the slot" to the caller. (A present zero-length value still RLP-encodes to 0x80, one byte,
/// so the empty return is unambiguous.)
bcos::bytes encodeStorageValue(bcos::bytesConstRef value);

/// The inverse of encodeStorageValue: decode one storage-trie leaf back into the slot value.
/// Kept next to the encoder so a change to one is impossible to miss in the other (proof
/// generation and historical reads both decode; only MPTBuilder encodes).
/// @throws MPTDecodeError on malformed RLP, on trailing bytes after the value, or when the value
/// is not a minimal big-endian byte string of at most 32 bytes (over-long or leading-zero
/// payloads decode into a plausible wrong value rather than an error if left unchecked).
bcos::u256 decodeStorageValue(bcos::bytesConstRef leaf);

}  // namespace bcos::ledger::mpt
