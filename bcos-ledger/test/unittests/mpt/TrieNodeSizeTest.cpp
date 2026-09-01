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
 * @file TrieNodeSizeTest.cpp
 * @brief Fixed-size NodeRef: sizeof budget + inline/hash/absent round-trips (spec §5.5, M12)
 */

#include <bcos-ledger/mpt/Errors.h>
#include <bcos-ledger/mpt/NodeDecoder.h>
#include <bcos-ledger/mpt/NodeEncoder.h>
#include <bcos-ledger/mpt/TrieMerge.h>
#include <bcos-ledger/mpt/TrieNode.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/test/unit_test.hpp>

#include "bcos-ledger/test/unittests/ExceptionCheck.h"

namespace bcos::ledger::mpt::test
{
using bcos::test::errinfoContains;

// The point of the fixed-size NodeRef: payload(32) + len(1) = 33 bytes flat, no heap. The old
// representation held bytes(24) + h256(32) + kind side by side → 64 bytes and one heap
// allocation per inline child; BranchNode carried 16 of them.
static_assert(sizeof(NodeRef) <= 40, "NodeRef must stay pointer-free and ~33 bytes");
// With children moved behind a vector (one 16 x 33B heap block), the BranchNode struct is just
// vector + bytes, and the TrieNode variant no longer pays BranchNode's inline footprint.
static_assert(sizeof(BranchNode) <= 56, "BranchNode struct = children vector + value bytes");
static_assert(sizeof(TrieNode) <= 64, "TrieNode variant must stay small (~56 bytes)");

BOOST_AUTO_TEST_SUITE(TrieNodeSizeSuite)

// Default construction is the absent-child sentinel and encodes as RLP-empty (0x80).
BOOST_AUTO_TEST_CASE(AbsentChildSemantics)
{
    NodeRef const ref = NodeRef::absent();
    BOOST_CHECK(ref.isAbsent());
    BOOST_CHECK(ref.kind() == NodeRef::Kind::Inline);
    BOOST_CHECK_EQUAL(ref.inlineRef().size(), 0U);

    // A branch of 16 absent children + empty value encodes as 17 x 0x80 behind the list header.
    bcos::bytes const raw = encodeRaw(TrieNode{BranchNode{}});
    BOOST_REQUIRE_EQUAL(raw.size(), 18U);
    for (size_t i = 1; i < raw.size(); ++i)
    {
        BOOST_CHECK_EQUAL(raw[i], 0x80);
    }

    // ... and decodes back to exactly 16 absent children (the invariant every consumer relies
    // on now that children is a vector).
    TrieNode const decoded = decodeNode(bcos::ref(raw));
    BOOST_REQUIRE_EQUAL(std::get<BranchNode>(decoded).children.size(), NIBBLE_RANGE);
    for (auto const& child : std::get<BranchNode>(decoded).children)
    {
        BOOST_CHECK(child.isAbsent());
    }
}

// The 16-slot invariant is convention now: default construction yields 16 slots, and the
// encoder must reject any other width instead of silently emitting a wrong-arity list.
BOOST_AUTO_TEST_CASE(BranchChildrenWidthInvariant)
{
    BOOST_CHECK_EQUAL(BranchNode{}.children.size(), NIBBLE_RANGE);

    BranchNode malformed;
    malformed.children.pop_back();
    BOOST_CHECK_EXCEPTION(encodeRaw(TrieNode{std::move(malformed)}), MPTInvariantViolation,
        [](auto const& e) { return errinfoContains(e, "must hold exactly NIBBLE_RANGE"); });
}

// Inline form survives encoder → decoder: a tiny leaf child comes back byte-identical.
BOOST_AUTO_TEST_CASE(InlineFormRoundTrip)
{
    LeafNode tiny;
    tiny.keyNibbles = {0xa};
    tiny.value = bcos::bytes{0x2a};
    bcos::bytes const tinyRaw = encodeRaw(TrieNode{tiny});
    BOOST_REQUIRE(tinyRaw.size() < bcos::h256::SIZE);

    auto const [raw, ref] = NodeEncoder<>::encodeAndRef(TrieNode{tiny});
    BOOST_CHECK(ref.kind() == NodeRef::Kind::Inline);
    BOOST_CHECK(!ref.isAbsent());
    BOOST_CHECK(ref.inlineRef().toBytes() == tinyRaw);

    BranchNode branch;
    branch.children[5] = ref;
    TrieNode const decoded = decodeNode(bcos::ref(encodeRaw(TrieNode{branch})));
    auto const& got = std::get<BranchNode>(decoded);
    BOOST_CHECK(got.children[5].kind() == NodeRef::Kind::Inline);
    BOOST_CHECK(got.children[5].inlineRef().toBytes() == tinyRaw);
}

// Hash form survives encoder → decoder: a >= 32-byte leaf child comes back as the same digest.
BOOST_AUTO_TEST_CASE(HashFormRoundTrip)
{
    LeafNode big;
    big.keyNibbles = {1, 2, 3, 4};
    big.value = bcos::bytes(40, 0xee);

    auto const [raw, ref] = NodeEncoder<>::encodeAndRef(TrieNode{big});
    BOOST_REQUIRE(raw.size() >= bcos::h256::SIZE);
    BOOST_CHECK(ref.kind() == NodeRef::Kind::Hash);
    BOOST_CHECK(!ref.isAbsent());
    BOOST_CHECK(ref.hash() != bcos::h256{});

    BranchNode branch;
    branch.children[9] = ref;
    TrieNode const decoded = decodeNode(bcos::ref(encodeRaw(TrieNode{branch})));
    auto const& got = std::get<BranchNode>(decoded);
    BOOST_CHECK(got.children[9].kind() == NodeRef::Kind::Hash);
    BOOST_CHECK(got.children[9].hash() == ref.hash());
}

// refToRawBytes / refFromRawBytes (ExtensionNode.child glue) invert each other in both forms.
BOOST_AUTO_TEST_CASE(RawBytesGlueRoundTrip)
{
    NodeRef const hashRef = NodeRef::fromHash(
        bcos::h256("0x3333333333333333333333333333333333333333333333333333333333333333"));
    bcos::bytes const hashRaw = detail::refToRawBytes(hashRef);
    BOOST_REQUIRE_EQUAL(hashRaw.size(), 33U);
    BOOST_CHECK_EQUAL(hashRaw[0], 0xa0);
    NodeRef const hashBack = detail::refFromRawBytes(hashRaw);
    BOOST_CHECK(hashBack.kind() == NodeRef::Kind::Hash);
    BOOST_CHECK(hashBack.hash() == hashRef.hash());

    bcos::bytes const tinyRaw = encodeRaw(TrieNode{LeafNode{.keyNibbles = {7}, .value = {0x01}}});
    NodeRef const inlineRef = NodeRef::fromInline(bcos::ref(tinyRaw));
    bcos::bytes const inlineRaw = detail::refToRawBytes(inlineRef);
    BOOST_CHECK(inlineRaw == tinyRaw);
    NodeRef const inlineBack = detail::refFromRawBytes(inlineRaw);
    BOOST_CHECK(inlineBack.kind() == NodeRef::Kind::Inline);
    BOOST_CHECK(inlineBack.inlineRef().toBytes() == tinyRaw);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
