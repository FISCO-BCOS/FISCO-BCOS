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
#include "Constants.h"
#include "HexPrefix.h"
#include "bcos-utilities/Overloaded.h"
#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-utilities/Common.h>

namespace bcos::ledger::mpt
{

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Appends the RLP encoding of a NodeRef child into dst.
// - Absent (len == 0)   → RLP empty string 0x80
// - Inline (len 1..31)  → splice the raw bytes as-is (already a complete RLP item)
// - Hash   (len == 32)  → encode the 32-byte hash as an RLP byte-string (33 bytes)
static void appendChildRef(bcos::bytes& dst, NodeRef const& ref)
{
    if (ref.kind() == NodeRef::Kind::Inline)
    {
        if (ref.isAbsent())
        {
            dst.push_back(RLP_EMPTY_STRING);  // absent child
        }
        else
        {
            auto const raw = ref.inlineRef();
            dst.insert(dst.end(), raw.begin(), raw.end());
        }
    }
    else
    {
        // Hash kind: emit as 33-byte RLP byte-string [0xa0, hash[0..31]]
        bcos::codec::rlp::encode(dst, ref.hash());
    }
}

// Encode EmptyNode → single-byte RLP empty string {0x80}
static void encodeEmpty(EmptyNode const& /*node*/, bcos::bytes& out)
{
    out.push_back(RLP_EMPTY_STRING);
}

// Encode LeafNode → RLP list [HP(keyNibbles, leaf=true), value]
static void encodeLeaf(LeafNode const& node, bcos::bytes& out)
{
    const auto hpe = hexPrefixEncode(bcos::ref(node.keyNibbles), /*isLeaf=*/true);
    // Two-argument encode() wraps in an RLP list automatically.
    bcos::codec::rlp::encode(out, hpe, node.value);
}

// Encode ExtensionNode → RLP list [HP(sharedNibbles, leaf=false), child_ref_raw]
// ExtensionNode.child is ALREADY a complete RLP-encoded child reference, so we must
// splice it raw rather than re-encode it as a byte-string (which would double-wrap).
static void encodeExtension(ExtensionNode const& node, bcos::bytes& out)
{
    using namespace bcos::codec::rlp;
    const auto hpe = hexPrefixEncode(bcos::ref(node.sharedNibbles), /*isLeaf=*/false);
    // child contributes its own bytes directly (not as an RLP-wrapped byte-string).
    const size_t payloadLen = length(hpe) + node.child.size();
    encodeHeader(out, {.isList = true, .payloadLength = payloadLen});
    encode(out, hpe);
    out.insert(out.end(), node.child.begin(), node.child.end());
}

// Encode BranchNode → RLP list [child0..child15, value]
static void encodeBranch(BranchNode const& node, bcos::bytes& out)
{
    using namespace bcos::codec::rlp;

    // RLP list header needs payload length up front, so the 17-item payload is built into a
    // local buffer first. Once sealed, the payload is spliced into `out` — caller pays one
    // allocation for `payload` per BranchNode regardless of how many parents are chained.
    bcos::bytes payload;
    for (NodeRef const& child : node.children)
    {
        appendChildRef(payload, child);
    }
    // 17th entry: the branch node's own value (empty in most internal nodes)
    encode(payload, node.value);

    encodeHeader(out, {.isList = true, .payloadLength = payload.size()});
    out.insert(out.end(), payload.begin(), payload.end());
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void encodeRaw(TrieNode const& node, bcos::bytes& out)
{
    std::visit(bcos::overloaded{
                   [&out](EmptyNode const& n) { encodeEmpty(n, out); },
                   [&out](LeafNode const& n) { encodeLeaf(n, out); },
                   [&out](ExtensionNode const& n) { encodeExtension(n, out); },
                   [&out](BranchNode const& n) { encodeBranch(n, out); },
               },
        node);
}

}  // namespace bcos::ledger::mpt
