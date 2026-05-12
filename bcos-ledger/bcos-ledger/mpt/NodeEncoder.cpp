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
 * @file NodeEncoder.cpp
 * @brief RLP encoding and NodeRef computation for MPT nodes (spec §5.5)
 */

#include "NodeEncoder.h"
#include "HexPrefix.h"
#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <bcos-utilities/Common.h>

namespace bcos::ledger::mpt
{

// ---------------------------------------------------------------------------
// keccak256 — Ethereum-flavour (0x01 domain padding, not NIST 0x06)
// ---------------------------------------------------------------------------

Hash32 keccak256(std::span<bcos::byte const> in)
{
    bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher hasher;
    // bytesConstRef satisfies the TrivialObject Range concept and is the idiomatic input type.
    hasher.update(bcos::bytesConstRef(in.data(), in.size()));
    Hash32 out;
    hasher.final(out);
    return out;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Appends the RLP encoding of a NodeRef child into dst.
// - Default (Inline + empty bytes) → RLP empty string 0x80 (absent child)
// - Inline non-empty              → splice the raw bytes as-is (already a complete RLP item)
// - Hash                          → encode the 32-byte hash as an RLP byte-string (33 bytes)
static void appendChildRef(bcos::bytes& dst, NodeRef const& ref)
{
    if (ref.kind == NodeRef::Kind::Inline)
    {
        if (ref.inlineBytes.empty())
        {
            dst.push_back(0x80);  // RLP empty string = absent child
        }
        else
        {
            dst.insert(dst.end(), ref.inlineBytes.begin(), ref.inlineBytes.end());
        }
    }
    else
    {
        // Hash kind: emit as 33-byte RLP byte-string [0xa0, hash[0..31]]
        bcos::codec::rlp::encode(dst, ref.hash);
    }
}

// Encode EmptyNode → single-byte RLP empty string {0x80}
static bcos::bytes encodeEmpty(EmptyNode const& /*node*/)
{
    return bcos::bytes{0x80};
}

// Encode LeafNode → RLP list [HP(keyNibbles, leaf=true), value]
static bcos::bytes encodeLeaf(LeafNode const& node)
{
    bcos::bytes out;
    auto hp = hexPrefixEncode(node.keyNibbles, /*terminator=*/true);
    // Two-argument encode() wraps in an RLP list automatically.
    bcos::codec::rlp::encode(out, hp, node.value);
    return out;
}

// Encode ExtensionNode → RLP list [HP(sharedNibbles, leaf=false), child_ref_raw]
// ExtensionNode.child is ALREADY a complete RLP-encoded child reference, so we must
// splice it raw rather than re-encode it as a byte-string (which would double-wrap).
static bcos::bytes encodeExtension(ExtensionNode const& node)
{
    using namespace bcos::codec::rlp;
    bcos::bytes out;
    auto hp = hexPrefixEncode(node.sharedNibbles, /*terminator=*/false);
    // child contributes its own bytes directly (not as an RLP-wrapped byte-string).
    const size_t payloadLen = length(hp) + node.child.size();
    encodeHeader(out, {.isList = true, .payloadLength = payloadLen});
    encode(out, hp);
    out.insert(out.end(), node.child.begin(), node.child.end());
    return out;
}

// Encode BranchNode → RLP list [child0..child15, value]
static bcos::bytes encodeBranch(BranchNode const& node)
{
    using namespace bcos::codec::rlp;

    // Build the 17-item payload bytes first, then wrap with a list header.
    bcos::bytes payload;
    for (NodeRef const& child : node.children)
    {
        appendChildRef(payload, child);
    }
    // 17th entry: the branch node's own value (empty in most internal nodes)
    encode(payload, node.value);

    bcos::bytes out;
    encodeHeader(out, {.isList = true, .payloadLength = payload.size()});
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

// ---------------------------------------------------------------------------
// NodeEncoder public API
// ---------------------------------------------------------------------------

bcos::bytes NodeEncoder::encodeRaw(TrieNode const& node)
{
    return std::visit(
        [](auto const& n) -> bcos::bytes {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, EmptyNode>)
            {
                return encodeEmpty(n);
            }
            else if constexpr (std::is_same_v<T, LeafNode>)
            {
                return encodeLeaf(n);
            }
            else if constexpr (std::is_same_v<T, ExtensionNode>)
            {
                return encodeExtension(n);
            }
            else if constexpr (std::is_same_v<T, BranchNode>)
            {
                return encodeBranch(n);
            }
            else
            {
                static_assert(!sizeof(T*), "unhandled TrieNode variant alternative");
            }
        },
        node);
}

std::pair<bcos::bytes, NodeRef> NodeEncoder::encodeAndRef(TrieNode const& node)
{
    bcos::bytes raw = encodeRaw(node);
    NodeRef ref;
    if (raw.size() < 32)
    {
        ref.kind = NodeRef::Kind::Inline;
        ref.inlineBytes = raw;
    }
    else
    {
        ref.kind = NodeRef::Kind::Hash;
        ref.hash = keccak256(std::span<bcos::byte const>(raw.data(), raw.size()));
    }
    return {std::move(raw), std::move(ref)};
}

}  // namespace bcos::ledger::mpt
