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
 * @file NodeDecoderTest.cpp
 * @brief Unit tests for TrieNode RLP decoding — round-trips against NodeEncoder (spec §5.5)
 */

#include <bcos-ledger/mpt/Errors.h>
#include <bcos-ledger/mpt/NodeDecoder.h>
#include <bcos-ledger/mpt/NodeEncoder.h>
#include <bcos-ledger/mpt/TrieNode.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/test/unit_test.hpp>

#include "bcos-ledger/test/unittests/ExceptionCheck.h"

namespace bcos::ledger::mpt::test
{
using bcos::test::errinfoContains;

BOOST_AUTO_TEST_SUITE(NodeDecoderSuite)

namespace
{
// A 33-byte RLP-encoded hash child reference: 0xa0 followed by 32 hash bytes.
bcos::bytes decoderHashChildRef(bcos::byte fill)
{
    bcos::bytes out{0xa0};
    out.insert(out.end(), 32, fill);
    return out;
}
}  // namespace

// EmptyNode decodes from the single byte {0x80}.
BOOST_AUTO_TEST_CASE(DecodeEmptyNode)
{
    TrieNode node = decodeNode(bcos::ref(bcos::bytes{0x80}));
    BOOST_CHECK(std::holds_alternative<EmptyNode>(node));
}

// Leaf round-trip, short value: encode → decode → re-encode must be byte-identical.
BOOST_AUTO_TEST_CASE(RoundTripLeafShort)
{
    LeafNode leaf;
    leaf.keyNibbles = {1, 2, 3, 4, 5};  // odd count exercises HP odd-path
    leaf.value = bcos::bytes{0xaa, 0xbb};

    bcos::bytes raw = encodeRaw(TrieNode{leaf});
    TrieNode decoded = decodeNode(bcos::ref(raw));

    BOOST_REQUIRE(std::holds_alternative<LeafNode>(decoded));
    BOOST_CHECK(std::get<LeafNode>(decoded).keyNibbles == leaf.keyNibbles);
    BOOST_CHECK(std::get<LeafNode>(decoded).value == leaf.value);
    BOOST_CHECK(encodeRaw(decoded) == raw);
}

// Leaf round-trip with a >=32 byte value so the leaf itself encodes large (long-string value head).
BOOST_AUTO_TEST_CASE(RoundTripLeafLarge)
{
    LeafNode leaf;
    leaf.keyNibbles = {0x0a, 0x0b, 0x0c, 0x0d};
    leaf.value = bcos::bytes(64, 0xee);  // forces a long byte-string value

    bcos::bytes raw = encodeRaw(TrieNode{leaf});
    BOOST_CHECK(raw.size() >= 32);
    TrieNode decoded = decodeNode(bcos::ref(raw));

    BOOST_REQUIRE(std::holds_alternative<LeafNode>(decoded));
    BOOST_CHECK(std::get<LeafNode>(decoded).value == leaf.value);
    BOOST_CHECK(encodeRaw(decoded) == raw);
}

// Extension whose child is a Hash ref (0xa0 + 32 bytes).
BOOST_AUTO_TEST_CASE(RoundTripExtensionHashChild)
{
    ExtensionNode ext;
    ext.sharedNibbles = {3, 4, 5};
    ext.child = decoderHashChildRef(0x77);

    bcos::bytes raw = encodeRaw(TrieNode{ext});
    TrieNode decoded = decodeNode(bcos::ref(raw));

    BOOST_REQUIRE(std::holds_alternative<ExtensionNode>(decoded));
    BOOST_CHECK(std::get<ExtensionNode>(decoded).sharedNibbles == ext.sharedNibbles);
    BOOST_CHECK(std::get<ExtensionNode>(decoded).child == ext.child);
    BOOST_CHECK(encodeRaw(decoded) == raw);
}

// Extension whose child is an inline small node (a short leaf encoded < 32 bytes, spliced raw).
BOOST_AUTO_TEST_CASE(RoundTripExtensionInlineChild)
{
    LeafNode inlineLeaf;
    inlineLeaf.keyNibbles = {1, 2};
    inlineLeaf.value = bcos::bytes{0x42};
    bcos::bytes inlineRaw = encodeRaw(TrieNode{inlineLeaf});
    BOOST_REQUIRE(inlineRaw.size() < 32);

    ExtensionNode ext;
    ext.sharedNibbles = {9};
    ext.child = inlineRaw;  // raw inline child node bytes

    bcos::bytes raw = encodeRaw(TrieNode{ext});
    TrieNode decoded = decodeNode(bcos::ref(raw));

    BOOST_REQUIRE(std::holds_alternative<ExtensionNode>(decoded));
    BOOST_CHECK(std::get<ExtensionNode>(decoded).child == ext.child);
    BOOST_CHECK(encodeRaw(decoded) == raw);
}

// Branch with a mix of absent + Hash + Inline children and an empty value.
BOOST_AUTO_TEST_CASE(RoundTripBranchMixedChildren)
{
    // An inline child: a tiny leaf node encoded < 32 bytes.
    LeafNode tiny;
    tiny.keyNibbles = {7};
    tiny.value = bcos::bytes{0x01};
    bcos::bytes tinyRaw = encodeRaw(TrieNode{tiny});
    BOOST_REQUIRE(tinyRaw.size() < 32);

    BranchNode branch;
    // children[0] absent (default)
    branch.children[3].setHash(
        bcos::h256("0x2222222222222222222222222222222222222222222222222222222222222222"));
    branch.children[10].setInline(bcos::ref(tinyRaw));
    // value left empty

    bcos::bytes raw = encodeRaw(TrieNode{branch});
    TrieNode decoded = decodeNode(bcos::ref(raw));

    BOOST_REQUIRE(std::holds_alternative<BranchNode>(decoded));
    auto const& got = std::get<BranchNode>(decoded);
    // child 0 absent
    BOOST_CHECK(got.children[0].kind() == NodeRef::Kind::Inline);
    BOOST_CHECK(got.children[0].isAbsent());
    // child 3 hash
    BOOST_CHECK(got.children[3].kind() == NodeRef::Kind::Hash);
    BOOST_CHECK(got.children[3].hash() == branch.children[3].hash());
    // child 10 inline
    BOOST_CHECK(got.children[10].kind() == NodeRef::Kind::Inline);
    BOOST_CHECK(got.children[10].inlineRef().toBytes() == tinyRaw);
    BOOST_CHECK(got.value.empty());
    BOOST_CHECK(encodeRaw(decoded) == raw);
}

// Malformed input: a non-empty top-level string is not a valid node.
BOOST_AUTO_TEST_CASE(DecodeRejectsNonNodeString)
{
    BOOST_CHECK_EXCEPTION(decodeNode(bcos::ref(bcos::bytes{0x83, 0x01, 0x02, 0x03})),
        MPTDecodeError, [](auto const& e) { return errinfoContains(e, "not a node list"); });
}

// Malformed input: a truncated header (declares 5 payload bytes but only 2 present).
BOOST_AUTO_TEST_CASE(DecodeRejectsTruncatedItem)
{
    BOOST_CHECK_EXCEPTION(decodeNode(bcos::ref(bcos::bytes{0xc5, 0x01, 0x02})), MPTDecodeError,
        [](auto const& e) { return errinfoContains(e, "malformed RLP header"); });
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
