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
 * @file NodeEncoder.h
 * @brief RLP encoding and NodeRef computation for MPT nodes (spec §5.5)
 */
#pragma once

#include "TrieNode.h"
#include <bcos-crypto/hasher/AnyHasher.h>
#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <bcos-utilities/core/Common.h>
#include <utility>

namespace bcos::ledger::mpt
{

/// Append the raw RLP bytes for a TrieNode to `out` (no inline/hash decision is made).
///   EmptyNode     → {0x80}
///   LeafNode      → RLP list of [HP(key, leaf=true), value]
///   ExtensionNode → RLP list of [HP(key, leaf=false), child_ref_raw]
///   BranchNode    → RLP list of [child0..child15, value]
///
/// Primary form for hot paths that already own a destination buffer (e.g. HashBuilder
/// writing into a parent payload, eth_getProof concatenating encodings into an RLP list).
void encodeRaw(TrieNode const& node, bcos::bytes& out);

/// Convenience overload that allocates a fresh buffer. Equivalent to
/// `bcos::bytes out; encodeRaw(node, out); return out;`. Use for one-off encoding
/// or when downstream code already needs an owning copy (tests, encodeAndRef).
inline bcos::bytes encodeRaw(TrieNode const& node)
{
    bcos::bytes out;
    encodeRaw(node, out);
    return out;
}

/// Encodes MPT nodes and computes hash-vs-inline node references (Yellow Paper §D, 32-byte rule):
///   len(RLP(node)) <  32 → ref is Inline (raw bytes spliced into parent)
///   len(RLP(node)) >= 32 → ref is Hash  (hasher digest of the raw bytes)
///
/// NodeEncoder is a stateless utility — both overloads are static. HasherT defaults to
/// OpenSSL_Keccak256_Hasher (Ethereum 0x01 padding); use the hasher-injection overload to plug
/// in AnyHasher (or any type satisfying the Hasher concept) for testing or alternate digests.
///
/// Thread-safety: NodeEncoder itself holds no state. Hasher state (e.g. OpenSSL EVP_MD_CTX) is
/// not thread-safe, so each thread must own its own HasherT instance and pass it in. The
/// no-argument overload constructs a fresh hasher per call (safe but allocates EVP_MD_CTX each
/// time); hot paths should reuse a hasher across calls via the injection overload.
template <bcos::crypto::hasher::Hasher HasherT =
              bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher>
class NodeEncoder
{
public:
    /// Hasher-injection overload. Caller owns hasher lifetime; reuse it across calls in hot
    /// paths to amortise the per-hasher EVP_MD_CTX allocation cost.
    static std::pair<bcos::bytes, NodeRef> encodeAndRef(TrieNode const& node, HasherT& hasher)
    {
        bcos::bytes raw = encodeRaw(node);
        NodeRef ref;
        if (raw.size() < bcos::h256::SIZE)
        {
            ref.kind = NodeRef::Kind::Inline;
            ref.inlineBytes = raw;
        }
        else
        {
            ref.kind = NodeRef::Kind::Hash;
            bcos::crypto::hasher::hash(
                hasher, bcos::bytesConstRef(raw.data(), raw.size()), ref.hash);
        }
        return {std::move(raw), std::move(ref)};
    }

    /// Convenience overload: constructs a fresh HasherT per call. Use only for one-off or
    /// low-frequency calls (e.g. tests); hot paths should call the injection overload above.
    static std::pair<bcos::bytes, NodeRef> encodeAndRef(TrieNode const& node)
    {
        HasherT hasher;
        return encodeAndRef(node, hasher);
    }
};

}  // namespace bcos::ledger::mpt
