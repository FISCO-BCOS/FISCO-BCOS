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
 * @file HashBuilderTest.cpp
 * @brief Unit tests for commitTrie() canonical trie construction, from-empty path (spec §5.3)
 */

#include "TestHelpers.h"
#include <bcos-crypto/hasher/AnyHasher.h>
#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-ledger/mpt/Nibble.h>
#include <bcos-ledger/mpt/NodeEncoder.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <map>
#include <optional>
#include <set>

namespace bcos::ledger::mpt::test
{

BOOST_AUTO_TEST_SUITE(HashBuilderSuite)

namespace
{
using NodeStorage = bcos::storage2::memory_storage::MemoryStorage<bcos::h256, bcos::bytes>;

// keccak256 of `raw` using a fresh hasher (independent recompute for cache/drain checks).
bcos::h256 hbKeccak(bcos::bytes const& raw)
{
    bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher hasher;
    bcos::h256 out;
    bcos::crypto::hasher::hash(hasher, bcos::ref(raw), out);
    return out;
}

// Hash of a single leaf the way commitTrie does: encode → inline ? keccak(raw) : ref.hash.
bcos::h256 singleLeafRoot(bcos::h256 const& key, bcos::bytes const& value)
{
    LeafNode leaf;
    leaf.keyNibbles = bytesToNibbles(key.ref());
    leaf.value = value;
    bcos::bytes const raw = encodeRaw(TrieNode{leaf});
    bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher hasher;
    bcos::h256 out;
    bcos::crypto::hasher::hash(hasher, bcos::ref(raw), out);
    return out;
}

// Distinct random h256 keys + small random values, generated from a fixed seed.
std::vector<std::pair<bcos::h256, bcos::bytes>> randomKvs(size_t count, uint32_t seed)
{
    auto rng = seededRng(seed);
    std::uniform_int_distribution<int> byteDist(0, 255);
    std::uniform_int_distribution<int> valLenDist(1, 40);
    std::set<bcos::h256> seen;
    std::vector<std::pair<bcos::h256, bcos::bytes>> kvs;
    while (kvs.size() < count)
    {
        bcos::h256 key;
        for (size_t i = 0; i < bcos::h256::SIZE; ++i)
        {
            key.data()[i] = static_cast<bcos::byte>(byteDist(rng));
        }
        if (!seen.insert(key).second)
        {
            continue;
        }
        bcos::bytes value(static_cast<size_t>(valLenDist(rng)));
        for (auto& b : value)
        {
            b = static_cast<bcos::byte>(byteDist(rng));
        }
        kvs.emplace_back(key, std::move(value));
    }
    return kvs;
}
}  // namespace

// An empty change-set yields the canonical empty-trie root.
BOOST_AUTO_TEST_CASE(EmptyTrieReturnsEmptyRootHash)
{
    NodeStorage storage;
    auto result = commitTrieFlushed(
        storage, emptyRootHash(), std::map<bcos::h256, std::optional<bcos::bytes>>{});
    BOOST_CHECK_EQUAL(result.root, emptyRootHash());
    BOOST_CHECK(result.newNodes.empty());
    BOOST_CHECK(result.obsoletedNodes.empty());
}

// A single key+value must match an independently computed leaf root (anchors to M2 encoder).
BOOST_AUTO_TEST_CASE(SingleLeafMatchesNodeEncoder)
{
    NodeStorage storage;
    auto key = makeHash(0x42);
    bcos::bytes value{0xca, 0xfe, 0xba, 0xbe};

    auto result = seedTrieFlushed(storage, emptyRootHash(), {{key, value}});

    BOOST_CHECK_EQUAL(result.root, singleLeafRoot(key, value));
}

// Two keys sharing a long prefix, differing late → extension + branch + two leaves.
BOOST_AUTO_TEST_CASE(TwoKeysSharedPrefix)
{
    // Keys identical except in the final byte → 63 shared nibbles.
    bcos::h256 keyA{};
    bcos::h256 keyB{};
    for (size_t i = 0; i < bcos::h256::SIZE; ++i)
    {
        keyA.data()[i] = 0xab;
        keyB.data()[i] = 0xab;
    }
    keyB.data()[bcos::h256::SIZE - 1] = 0xcd;

    bcos::bytes valA{0x01, 0x02};
    bcos::bytes valB{0x03, 0x04};

    NodeStorage storage;
    auto result = seedTrieFlushed(storage, emptyRootHash(), {{keyA, valA}, {keyB, valB}});

    BOOST_CHECK_EQUAL(result.root, referenceRoot({{keyA, valA}, {keyB, valB}}));

    // The top node is large here, so the flushed root node must be retrievable from storage.
    auto cached = bcos::task::syncWait(bcos::storage2::readOne(storage, result.root));
    BOOST_REQUIRE(cached.has_value());
    BOOST_CHECK_EQUAL(hbKeccak(*cached), result.root);
}

// Main correctness gate: random batches across several sizes must match the independent
// reference oracle. The change-set is a std::map, so insertion order is normalized away by the
// container itself — fill it in shuffled order to document that property.
BOOST_AUTO_TEST_CASE(RandomBatchMatchesReference)
{
    for (size_t size : {size_t{1}, size_t{2}, size_t{5}, size_t{20}, size_t{100}})
    {
        auto kvs = randomKvs(size, /*seed=*/0x5eed + static_cast<uint32_t>(size));
        bcos::h256 const expected = referenceRoot(kvs);

        auto shuffled = kvs;
        auto rng = seededRng(0xC0FFEE + static_cast<uint32_t>(size));
        std::shuffle(shuffled.begin(), shuffled.end(), rng);

        std::map<bcos::h256, std::optional<bcos::bytes>> changes;
        for (auto const& [key, value] : shuffled)
        {
            changes[key] = value;
        }
        NodeStorage storage;
        auto result = commitTrieFlushed(storage, emptyRootHash(), changes);

        BOOST_CHECK_MESSAGE(result.root == expected,
            "root mismatch for size=" << size << " got=" << result.root.hex()
                                      << " expected=" << expected.hex());
    }
}

// A multi-node trie must produce new nodes; every (hash, bytes) must satisfy keccak(bytes)==hash,
// and a from-empty build obsoletes nothing.
BOOST_AUTO_TEST_CASE(NewNodesMatchTheirHashes)
{
    auto kvs = randomKvs(50, /*seed=*/0xABCD);

    std::map<bcos::h256, std::optional<bcos::bytes>> changes;
    for (auto const& [key, value] : kvs)
    {
        changes[key] = value;
    }
    NodeStorage storage;
    auto result = commitTrieFlushed(storage, emptyRootHash(), changes);

    BOOST_REQUIRE(!result.newNodes.empty());
    for (auto const& [hash, raw] : result.newNodes)
    {
        BOOST_CHECK_EQUAL(hbKeccak(raw), hash);
    }
    // The root must be among the produced nodes (top node is large for 50 keys).
    BOOST_CHECK(result.newNodes.find(result.root) != result.newNodes.end());
    BOOST_CHECK(result.obsoletedNodes.empty());
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
