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
 * @file NodeEncoderTest.cpp
 * @brief Unit tests for TrieNode RLP encoding and NodeRef computation (spec §5.5)
 */

#include <bcos-ledger/mpt/Constants.h>
#include <bcos-ledger/mpt/NodeEncoder.h>
#include <bcos-ledger/mpt/TrieNode.h>
#include <bcos-utilities/Common.h>
#include <boost/test/unit_test.hpp>

namespace bcos::test
{

BOOST_AUTO_TEST_SUITE(NodeEncoderSuite)

// ---------------------------------------------------------------------------
// Test 1: Short leaf → inline node reference
// ---------------------------------------------------------------------------
// LeafNode{keyNibbles={1,2,3,4}, value={0xaa}}
//   HP(keyNibbles=[1,2,3,4], leaf=true): even count → [0x20, 0x12, 0x34] (3 bytes)
//   RLP list [ 3-byte hp, 1-byte value ] payload = 4 + 2 = 6 bytes → total 7 bytes
//   7 < 32, so encodeAndRef must return Inline
BOOST_AUTO_TEST_CASE(EncodeLeafShort)
{
    using namespace bcos::ledger::mpt;

    LeafNode leaf;
    leaf.keyNibbles = {1, 2, 3, 4};
    leaf.value = bcos::bytes{0xaa};

    auto [raw, ref] = NodeEncoder::encodeAndRef(TrieNode{leaf});

    BOOST_CHECK(!raw.empty());
    BOOST_CHECK(raw.size() < 32);
    BOOST_CHECK(ref.kind == NodeRef::Kind::Inline);
    BOOST_CHECK(ref.inlineBytes == raw);
}

// ---------------------------------------------------------------------------
// Test 2: Large branch node → hash node reference
// ---------------------------------------------------------------------------
// The plan claimed a default BranchNode is "always hashed due to size", which is incorrect:
// 16 × 0x80 (absent) + 0x80 (empty value) = 17 bytes payload → 1 byte header → 18 bytes total
// (< 32, so inline).  The test is adapted: we set children[0] to a Hash-kind NodeRef with a
// 32-byte hash digest.  That child contributes [0xa0, hash×32] = 33 bytes, making the payload
// at least 33 + 15 + 1 = 49 bytes → total ≥ 50 bytes (> 32) → encodeAndRef returns Hash.
BOOST_AUTO_TEST_CASE(EncodeBranchHashedWhenLarge)
{
    using namespace bcos::ledger::mpt;

    BranchNode branch;
    // Set children[0] to a Hash-kind ref with a dummy 32-byte hash
    branch.children[0].kind = NodeRef::Kind::Hash;
    branch.children[0].hash =
        Hash32("0x1111111111111111111111111111111111111111111111111111111111111111");

    auto [raw, ref] = NodeEncoder::encodeAndRef(TrieNode{branch});

    // 33 (hash child) + 15 × 1 (absent children) + 1 (empty value) = 49 payload → 50 total
    BOOST_CHECK(raw.size() >= 32);
    BOOST_CHECK(ref.kind == NodeRef::Kind::Hash);
    // Hash must be exactly 32 bytes
    BOOST_CHECK_EQUAL(ref.hash.size(), 32u);
}

// ---------------------------------------------------------------------------
// Test 3: Empty trie root matches Ethereum well-known constant
// ---------------------------------------------------------------------------
// EmptyNode encodes to RLP empty string {0x80} (1 byte, < 32 → inline).
// keccak256({0x80}) must equal the well-known Ethereum empty-trie root:
//   0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421
// This test validates that the OpenSSLHasher applies the Ethereum 0x01 domain padding,
// not the NIST SHA3-256 0x06 padding.
BOOST_AUTO_TEST_CASE(EmptyTrieRootMatchesEthereumConstant)
{
    using namespace bcos::ledger::mpt;

    bcos::bytes raw = NodeEncoder::encodeRaw(TrieNode{EmptyNode{}});
    BOOST_CHECK_EQUAL(raw.size(), 1u);
    BOOST_CHECK_EQUAL(raw[0], 0x80u);

    Hash32 digest = keccak256(std::span<bcos::byte const>(raw.data(), raw.size()));
    BOOST_CHECK_EQUAL(digest, emptyRootHash());
}

// ---------------------------------------------------------------------------
// Test 4: Inline / hash threshold at 32 bytes
// ---------------------------------------------------------------------------
// Using keyNibbles={1,2,3,4} (HP = [0x20,0x12,0x34], 3 bytes; RLP(hp)=4 bytes):
//   value = bytes(25, 0xff): RLP(value) = 1+25 = 26 bytes
//     payload = 4+26 = 30, list = 1+30 = 31 bytes < 32  → Inline
//   value = bytes(26, 0xff): RLP(value) = 1+26 = 27 bytes
//     payload = 4+27 = 31, list = 1+31 = 32 bytes >= 32 → Hash
BOOST_AUTO_TEST_CASE(InlineThresholdAt31Bytes)
{
    using namespace bcos::ledger::mpt;

    std::vector<uint8_t> const key = {1, 2, 3, 4};

    // 31-byte encoding → Inline
    {
        LeafNode leaf;
        leaf.keyNibbles = key;
        leaf.value = bcos::bytes(25, 0xff);

        auto [raw, ref] = NodeEncoder::encodeAndRef(TrieNode{leaf});
        BOOST_CHECK_EQUAL(raw.size(), 31u);
        BOOST_CHECK(ref.kind == NodeRef::Kind::Inline);
        BOOST_CHECK(ref.inlineBytes == raw);
    }

    // 32-byte encoding → Hash
    {
        LeafNode leaf;
        leaf.keyNibbles = key;
        leaf.value = bcos::bytes(26, 0xff);

        auto [raw, ref] = NodeEncoder::encodeAndRef(TrieNode{leaf});
        BOOST_CHECK_EQUAL(raw.size(), 32u);
        BOOST_CHECK(ref.kind == NodeRef::Kind::Hash);
        BOOST_CHECK_EQUAL(ref.hash.size(), 32u);
        BOOST_CHECK(ref.inlineBytes.empty());
    }
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
