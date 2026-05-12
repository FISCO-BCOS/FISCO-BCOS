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

#include "Types.h"
#include <bcos-utilities/Common.h>
#include <array>
#include <cstdint>
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
    std::vector<uint8_t> keyNibbles;  ///< Nibble sequence (no HP terminator; each in [0, 15])
    bcos::bytes value;                ///< Raw value bytes (encoded as RLP byte-string)
};

/// An extension node: stores a shared nibble prefix and an already-RLP-encoded child reference.
struct ExtensionNode
{
    std::vector<uint8_t> sharedNibbles;  ///< Shared nibble prefix (no HP terminator)
    bcos::bytes child;  ///< Already RLP-encoded child ref (inline bytes or 33-byte hash string)
};

/// A node reference used inside BranchNode children.
/// - Inline: the referenced subtree's complete RLP encoding is short (<32 bytes) and is stored
///   directly; inlineBytes may be empty, which represents an absent child (treated as empty).
/// - Hash:   the referenced subtree is ≥32 bytes; only the 32-byte keccak256 digest is stored.
struct NodeRef
{
    enum class Kind
    {
        Inline,
        Hash
    };

    Kind kind{Kind::Inline};
    bcos::bytes inlineBytes;  ///< Valid when kind == Inline. An empty inlineBytes
                              ///< with kind == Inline encodes as RLP-empty (0x80) and
                              ///< represents an absent branch child.
    Hash32 hash{};            ///< Valid when kind == Hash; keccak256(child RLP)
};

/// A branch node: 16 child references (one per hex nibble) plus an optional value.
struct BranchNode
{
    std::array<NodeRef, 16> children;  ///< Default-constructed = Inline + empty (absent child)
    bcos::bytes value;                 ///< Typically empty for internal branch nodes
};

/// Tagged union of all MPT node types.
using TrieNode = std::variant<EmptyNode, LeafNode, ExtensionNode, BranchNode>;

}  // namespace bcos::ledger::mpt
