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
#include <bcos-codec/rlp/Common.h>
#include <bcos-utilities/FixedBytes.h>

namespace bcos::ledger::mpt
{

namespace
{

// A parsed RLP item header. The item occupies the raw span [pos, pos + headerLen + payloadLen);
// for a string the content is [pos + headerLen, pos + headerLen + payloadLen) — except a
// single-byte string (b < 0x80) whose content is the one byte at `pos` (headerLen == 0).
struct DecoderItemView
{
    bool isList{false};
    size_t headerLen{0};
    size_t payloadLen{0};
};

// Reads `count` big-endian bytes starting at raw[pos] into a size_t. Throws if the span is
// truncated. `count` is at most 8 (RLP long-form length-of-length never needs more for our sizes).
size_t readBigEndianLen(bcos::bytesConstRef raw, size_t pos, size_t count)
{
    if (count == 0 || count > sizeof(size_t) || pos + count > raw.size())
    {
        BOOST_THROW_EXCEPTION(
            MPTDecodeError{} << bcos::errinfo_comment("RLP length-of-length truncated"));
    }
    size_t value = 0;
    for (size_t i = 0; i < count; ++i)
    {
        value = (value << 8) | static_cast<size_t>(raw[pos + i]);
    }
    return value;
}

// Parse the RLP header of the item that starts at raw[pos]. Throws MPTDecodeError if the declared
// span runs past raw.size().
DecoderItemView parseItem(bcos::bytesConstRef raw, size_t pos)
{
    using namespace bcos::codec::rlp;
    if (pos >= raw.size())
    {
        BOOST_THROW_EXCEPTION(
            MPTDecodeError{} << bcos::errinfo_comment("RLP item starts past end of input"));
    }

    bcos::byte const b = raw[pos];
    DecoderItemView view;
    if (b < BYTES_HEAD_BASE)  // < 0x80: single-byte string, the byte IS the content
    {
        view = {.isList = false, .headerLen = 0, .payloadLen = 1};
    }
    else if (b <= LONG_BYTES_HEAD_BASE)  // 0x80..0xb7: short string
    {
        view = {.isList = false, .headerLen = 1, .payloadLen = static_cast<size_t>(b) - BYTES_HEAD_BASE};
    }
    else if (b < LIST_HEAD_BASE)  // 0xb8..0xbf: long string
    {
        size_t const lenOfLen = static_cast<size_t>(b) - LONG_BYTES_HEAD_BASE;
        view = {.isList = false,
            .headerLen = 1 + lenOfLen,
            .payloadLen = readBigEndianLen(raw, pos + 1, lenOfLen)};
    }
    else if (b <= LONG_LIST_HEAD_BASE)  // 0xc0..0xf7: short list
    {
        view = {.isList = true, .headerLen = 1, .payloadLen = static_cast<size_t>(b) - LIST_HEAD_BASE};
    }
    else  // 0xf8..0xff: long list
    {
        size_t const lenOfLen = static_cast<size_t>(b) - LONG_LIST_HEAD_BASE;
        view = {.isList = true,
            .headerLen = 1 + lenOfLen,
            .payloadLen = readBigEndianLen(raw, pos + 1, lenOfLen)};
    }

    if (pos + view.headerLen + view.payloadLen > raw.size())
    {
        BOOST_THROW_EXCEPTION(
            MPTDecodeError{} << bcos::errinfo_comment("RLP item span exceeds input length"));
    }
    return view;
}

// One child item discovered while walking a list payload: its full RLP raw span, its string
// content span, and whether the item is itself a list.
struct DecoderChild
{
    bcos::bytesConstRef raw;      // [pos, pos + headerLen + payloadLen)
    bcos::bytesConstRef content;  // [pos + headerLen, pos + headerLen + payloadLen), or the lone byte
    bool isList{false};
    size_t payloadLen{0};
};

// Walk the payload region [start, end) of a list, collecting one DecoderChild per item.
std::vector<DecoderChild> walkList(bcos::bytesConstRef raw, size_t start, size_t end)
{
    std::vector<DecoderChild> items;
    size_t pos = start;
    while (pos < end)
    {
        DecoderItemView const view = parseItem(raw, pos);
        size_t const total = view.headerLen + view.payloadLen;
        DecoderChild child;
        child.raw = raw.getCroppedData(pos, total);
        child.isList = view.isList;
        child.payloadLen = view.payloadLen;
        if (view.headerLen == 0)  // single-byte string: content is the lone byte at pos
        {
            child.content = raw.getCroppedData(pos, 1);
        }
        else
        {
            child.content = raw.getCroppedData(pos + view.headerLen, view.payloadLen);
        }
        items.push_back(child);
        pos += total;
    }
    if (pos != end)
    {
        BOOST_THROW_EXCEPTION(
            MPTDecodeError{} << bcos::errinfo_comment("RLP list payload not exactly consumed"));
    }
    return items;
}

bcos::h256 toHash(bcos::bytesConstRef content)
{
    return bcos::h256{content.data(), content.size()};
}

bcos::bytes toBytes(bcos::bytesConstRef ref)
{
    return bcos::bytes{ref.begin(), ref.end()};
}

}  // namespace

TrieNode decodeNode(bcos::bytesConstRef raw)
{
    DecoderItemView const top = parseItem(raw, 0);

    // 1) Not a list → must be the empty-string EmptyNode {0x80}; any other string is malformed.
    if (!top.isList)
    {
        if (top.payloadLen == 0 && top.headerLen == 1)
        {
            return EmptyNode{};
        }
        BOOST_THROW_EXCEPTION(MPTDecodeError{} << bcos::errinfo_comment(
                                  "top-level RLP item is a non-empty string, not a node list"));
    }

    // 2) Walk the list payload.
    std::vector<DecoderChild> items =
        walkList(raw, top.headerLen, top.headerLen + top.payloadLen);

    // 3) Two items → Leaf or Extension, distinguished by the HP terminator flag.
    if (items.size() == 2)
    {
        auto [nibbles, isLeaf] = hexPrefixDecode(items[0].content);
        if (isLeaf)
        {
            LeafNode leaf;
            leaf.keyNibbles = std::move(nibbles);
            leaf.value = toBytes(items[1].content);  // value byte-string content
            return leaf;
        }
        ExtensionNode ext;
        ext.sharedNibbles = std::move(nibbles);
        ext.child = toBytes(items[1].raw);  // keep the child RLP-encoded (raw span)
        return ext;
    }

    // 4) Seventeen items → Branch: 16 children + value.
    if (items.size() == NIBBLE_RANGE + 1)
    {
        BranchNode branch;
        for (size_t i = 0; i < NIBBLE_RANGE; ++i)
        {
            DecoderChild const& child = items[i];
            if (!child.isList && child.payloadLen == 0)
            {
                branch.children[i] = NodeRef::absent();
            }
            else if (!child.isList && child.payloadLen == bcos::h256::SIZE)
            {
                branch.children[i].kind = NodeRef::Kind::Hash;
                branch.children[i].hash = toHash(child.content);
            }
            else
            {
                // Inline subtree (a list) or any other non-empty/non-32 string: keep the raw span.
                branch.children[i].kind = NodeRef::Kind::Inline;
                branch.children[i].inlineBytes = toBytes(child.raw);
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
