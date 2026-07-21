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
 * @file TrieNode.h
 * @brief In-memory representation of the three MPT node types (spec §5.5)
 */
#pragma once

#include "Nibble.h"
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <variant>
#include <vector>

namespace bcos::ledger::mpt
{

/// The empty trie node — its RLP encoding is the single byte 0x80 (RLP empty string).
struct EmptyNode
{
};

/// A leaf node: stores a path suffix (nibble-space, no terminator) and a value.
struct LeafNode
{
    /// Nibble sequence (no HP terminator; each in [0, 15])
    bcos::bytes keyNibbles;
    /// Raw value bytes (encoded as RLP byte-string)
    bcos::bytes value;
};

/// An extension node: stores a shared nibble prefix and an already-RLP-encoded child reference.
struct ExtensionNode
{
    /// Shared nibble prefix (no HP terminator)
    bcos::bytes sharedNibbles;
    /// Already RLP-encoded child ref (inline bytes or 33-byte hash string)
    bcos::bytes child;
};

/// A node reference used inside BranchNode children.
/// - Inline: the referenced subtree's complete RLP encoding is short (< 32 bytes by the Yellow
///   Paper §D 32-byte rule) and is stored directly; a zero-length inline ref encodes as RLP-empty
///   (0x80) and represents an absent branch child.
/// - Hash:   the referenced subtree's RLP is ≥ 32 bytes; only the 32-byte digest is stored.
///
/// Both forms fit one fixed 32-byte buffer, so NodeRef is 33 bytes flat (no heap allocation) —
/// `len` alone discriminates: 0 = absent, 1..31 = inline RLP of that length, 32 = hash.
struct NodeRef
{
    enum class Kind : uint8_t
    {
        Inline,
        Hash
    };

    /// Inline: the first `len` bytes are the child's complete RLP; the tail is zero.
    /// Hash: all 32 bytes are the digest of the child's RLP.
    std::array<bcos::byte, bcos::h256::SIZE> payload{};
    /// 0 = absent child; 1..31 = inline RLP length; 32 = hash.
    uint8_t len = 0;

    [[nodiscard]] Kind kind() const noexcept
    {
        return len == bcos::h256::SIZE ? Kind::Hash : Kind::Inline;
    }
    /// Absent child sentinel (inline, zero length); encodes as RLP-empty (0x80).
    [[nodiscard]] bool isAbsent() const noexcept { return len == 0; }

    /// Valid when kind() == Inline: view of the child's complete RLP encoding.
    [[nodiscard]] bcos::bytesConstRef inlineRef() const noexcept { return {payload.data(), len}; }
    /// Valid when kind() == Hash: the 32-byte digest, copied out.
    [[nodiscard]] bcos::h256 hash() const noexcept
    {
        return bcos::h256{payload.data(), payload.size()};
    }

    /// Caller guarantees raw.size() < 32 (Yellow Paper rule; untrusted inputs are rejected
    /// upstream in NodeDecoder / refFromRawBytes before reaching here).
    void setInline(bcos::bytesConstRef raw) noexcept
    {
        assert(raw.size() < bcos::h256::SIZE);
        std::copy(raw.begin(), raw.end(), payload.begin());
        std::fill(payload.begin() + raw.size(), payload.end(), bcos::byte{0});
        len = static_cast<uint8_t>(raw.size());
    }
    void setHash(bcos::h256 const& digest) noexcept
    {
        std::copy(digest.begin(), digest.end(), payload.begin());
        len = static_cast<uint8_t>(bcos::h256::SIZE);
    }

    static NodeRef fromInline(bcos::bytesConstRef raw) noexcept
    {
        NodeRef ref;
        ref.setInline(raw);
        return ref;
    }
    static NodeRef fromHash(bcos::h256 const& digest) noexcept
    {
        NodeRef ref;
        ref.setHash(digest);
        return ref;
    }

    /// Explicit factory for the absent-child sentinel. Identical to default-construction;
    /// named for self-documenting call sites.
    static NodeRef absent() noexcept { return NodeRef{}; }
};

/// A branch node: 16 child references (one per hex nibble) plus an optional value.
///
/// children is a vector (one heap block of 16 x 33B) rather than std::array so the TrieNode
/// variant itself stays small — every LeafNode/ExtensionNode instance, by-value TrieNode move
/// and Task<TrieNode> coroutine frame would otherwise pay BranchNode's full inline footprint.
/// The "exactly NIBBLE_RANGE slots" invariant moves from the type to convention: the default
/// member initializer below makes every default-constructed branch 16-wide (each slot the
/// absent-child sentinel), and NodeEncoder rejects any other size before encoding.
struct BranchNode
{
    /// Always NIBBLE_RANGE entries; default-constructed entries = absent child (len == 0)
    std::vector<NodeRef> children = std::vector<NodeRef>(NIBBLE_RANGE);
    ///< Typically empty for internal branch nodes
    bcos::bytes value;
};

/// Tagged union of all MPT node types.
using TrieNode = std::variant<EmptyNode, LeafNode, ExtensionNode, BranchNode>;

}  // namespace bcos::ledger::mpt
