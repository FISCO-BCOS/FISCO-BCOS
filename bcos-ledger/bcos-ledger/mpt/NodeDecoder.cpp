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
 * @file NodeDecoder.cpp
 * @brief RLP node decoding for MPT nodes — inverse of NodeEncoder (spec §5.5)
 */

#include "NodeDecoder.h"
#include "Errors.h"
#include "HexPrefix.h"
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-utilities/FixedBytes.h>
#include <vector>

namespace bcos::ledger::mpt
{

namespace
{

// A Leaf or Extension node is a 2-item RLP list; a Branch is NIBBLE_RANGE + 1 items.
constexpr size_t LEAF_OR_EXTENSION_ITEM_COUNT = 2;

// One RLP item read out of a buffer: its full raw span (header + payload), its string content span
// (payload only; for a single byte < 0x80 the lone byte itself), and whether the item is a list.
// MPT keeps branch/extension child references RLP-encoded, so we must track the raw span — the
// high-level codec::rlp::decode strips headers and rejects lists-into-byte-strings, hence we drop
// to decodeHeader and capture spans ourselves.
struct DecoderItem
{
    bcos::bytesConstRef raw;
    bcos::bytesConstRef content;
    bool isList{false};
};

// Read one RLP item from `cursor`, advancing it past the whole item. All head-byte / long-length
// parsing is delegated to the shared bcos::codec::rlp::decodeHeader (which also bounds-checks the
// payload against the remaining input). Throws MPTDecodeError on malformed input.
DecoderItem readItem(bcos::bytesRef& cursor)
{
    bcos::byte const* const itemStart = cursor.data();
    auto&& [error, header] = bcos::codec::rlp::decodeHeader(cursor);
    if (error)
    {
        BOOST_THROW_EXCEPTION(MPTDecodeError{} << bcos::errinfo_comment(
                                  "malformed RLP header while decoding MPT node"));
    }
    // decodeHeader consumed the header (0 bytes for a single byte < 0x80) so cursor now starts at
    // the payload; headerLen is how far it advanced.
    auto const headerLen = static_cast<size_t>(cursor.data() - itemStart);
    bcos::bytesConstRef const content(cursor.data(), header.payloadLength);
    bcos::bytesConstRef const raw(itemStart, headerLen + header.payloadLength);
    cursor = cursor.getCroppedData(header.payloadLength);
    return DecoderItem{.raw = raw, .content = content, .isList = header.isList};
}

bcos::bytes toBytes(bcos::bytesConstRef ref)
{
    return bcos::bytes{ref.begin(), ref.end()};
}

}  // namespace

TrieNode decodeNode(bcos::bytesConstRef rawInput)
{
    // decodeHeader takes a mutable view but only advances it (never writes through the pointer);
    // MPT node buffers are always real mutable vectors, so this const_cast is sound.
    bcos::bytesRef cursor(const_cast<bcos::byte*>(rawInput.data()), rawInput.size());

    DecoderItem const top = readItem(cursor);

    // 1) Not a list → only the empty string {0x80} is a valid node (EmptyNode); any other string
    //    is malformed.
    if (!top.isList)
    {
        if (top.content.empty())
        {
            return EmptyNode{};
        }
        BOOST_THROW_EXCEPTION(MPTDecodeError{} << bcos::errinfo_comment(
                                  "top-level RLP item is a non-empty string, not a node list"));
    }

    // 2) Walk the list payload into its items.
    bcos::bytesRef payload(const_cast<bcos::byte*>(top.content.data()), top.content.size());
    std::vector<DecoderItem> items;
    while (!payload.empty())
    {
        items.push_back(readItem(payload));
    }

    // 3) Two items → Leaf or Extension, distinguished by the HP terminator flag.
    if (items.size() == LEAF_OR_EXTENSION_ITEM_COUNT)
    {
        auto [nibbles, isLeaf] = hexPrefixDecode(items[0].content);
        if (isLeaf)
        {
            return LeafNode{.keyNibbles = std::move(nibbles), .value = toBytes(items[1].content)};
        }
        // Keep the child RLP-encoded (raw span): it is itself a node reference, not a byte-string.
        return ExtensionNode{.sharedNibbles = std::move(nibbles), .child = toBytes(items[1].raw)};
    }

    // 4) Seventeen items → Branch: 16 children + value.
    if (items.size() == NIBBLE_RANGE + 1)
    {
        BranchNode branch;
        for (size_t i = 0; i < NIBBLE_RANGE; ++i)
        {
            DecoderItem const& child = items[i];
            if (!child.isList && child.content.empty())  // empty string 0x80 → absent child
            {
                branch.children[i] = NodeRef::absent();
            }
            else if (!child.isList && child.content.size() == bcos::h256::SIZE)  // 0xa0 + 32 bytes
            {
                branch.children[i].setHash(bcos::h256{child.content.data(), child.content.size()});
            }
            else  // inline subtree (a list) → keep the raw bytes
            {
                // Yellow Paper §D 32-byte rule: an embedded (inline) node's RLP is < 32 bytes;
                // anything larger must be hash-referenced, so reject it as malformed.
                if (child.raw.size() >= bcos::h256::SIZE)
                {
                    BOOST_THROW_EXCEPTION(MPTDecodeError{} << bcos::errinfo_comment(
                                              "branch child embeds an inline node of >= 32 bytes"));
                }
                branch.children[i].setInline(child.raw);
            }
        }
        branch.value = toBytes(items[NIBBLE_RANGE].content);
        return branch;
    }

    // 5) Any other item count is not a valid MPT node.
    BOOST_THROW_EXCEPTION(MPTDecodeError{} << bcos::errinfo_comment(
                              "RLP list has an item count that is not a valid MPT node (2 or 17)"));
}

}  // namespace bcos::ledger::mpt
