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
#include <bcos-utilities/Common.h>
#include <span>
#include <utility>

namespace bcos::ledger::mpt
{

/// Ethereum-flavour Keccak256 (0x01 padding, NOT NIST SHA3-256 which uses 0x06 padding).
/// @param in  Raw input bytes (bcos::byte = uint8_t)
/// @return    32-byte Keccak256 digest
Hash32 keccak256(std::span<bcos::byte const> in);

/// Encodes MPT nodes to RLP and determines inline-vs-hash node references.
///
/// The 32-byte threshold rule (Yellow Paper §D):
///   len(RLP(node)) <  32 → the node is referenced inline (bytes spliced directly into parent)
///   len(RLP(node)) >= 32 → the node is referenced by its keccak256 hash
class NodeEncoder
{
public:
    /// Compute the raw RLP bytes for a TrieNode without making an inline/hash decision.
    /// EmptyNode  → {0x80}
    /// LeafNode   → RLP list of [HP(key, leaf=true), value]
    /// ExtensionNode → RLP list of [HP(key, leaf=false), child_ref_raw]
    /// BranchNode → RLP list of [child0..child15, value]
    static bcos::bytes encodeRaw(TrieNode const& node);

    /// Encode a TrieNode and return both the raw RLP and a NodeRef for parent use.
    ///   If len(raw) <  32: ref.kind = Inline, ref.inlineBytes = raw
    ///   If len(raw) >= 32: ref.kind = Hash,   ref.hash = keccak256(raw)
    static std::pair<bcos::bytes, NodeRef> encodeAndRef(TrieNode const& node);
};

}  // namespace bcos::ledger::mpt
