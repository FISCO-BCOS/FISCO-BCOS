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
 * @file ComputeTrieRootTest.cpp
 * @brief Tests for the stateless computeTrieRoot core: equals the reference oracle and the stateful
 *        HashBuilder, and is reentrant (safe to run concurrently for independent inputs).
 */

#include "TestHelpers.h"
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-ledger/mpt/Errors.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <cstdint>
#include <map>
#include <thread>
#include <utility>
#include <vector>

namespace bcos::ledger::mpt::test
{

BOOST_AUTO_TEST_SUITE(ComputeTrieRootSuite)

namespace
{
// Distinct random h256 keys + small random values. Unique `ctr*` names avoid an anonymous-namespace
// ODR clash with the other mpt test files under UNITY_BUILD.
std::vector<std::pair<bcos::h256, bcos::bytes>> ctrRandomKvs(size_t count, uint32_t seed)
{
    auto rng = seededRng(seed);
    std::vector<std::pair<bcos::h256, bcos::bytes>> kvs;
    kvs.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        bcos::h256 key{};
        for (size_t j = 0; j < bcos::h256::SIZE; ++j)
        {
            key.data()[j] = static_cast<bcos::byte>(rng());
        }
        bcos::bytes value{static_cast<bcos::byte>(rng()), static_cast<bcos::byte>(rng()),
            static_cast<bcos::byte>(rng())};
        kvs.emplace_back(key, std::move(value));
    }
    return kvs;
}

// Collect kvs into a std::map: an ascending-by-keyHash range satisfying TrieEntryRange.
std::map<bcos::h256, bcos::bytes> ctrSortedMap(
    std::vector<std::pair<bcos::h256, bcos::bytes>> const& kvs)
{
    std::map<bcos::h256, bcos::bytes> sorted;
    for (auto const& [key, value] : kvs)
    {
        sorted[key] = value;
    }
    return sorted;
}
}  // namespace

// ---- computeTrieRootVarKey: variable-length-key build ----

// Var-key build agrees with the secure build for EQUAL-LENGTH keys: when every key is 32 bytes,
// bytesToNibbles produces the same 64-nibble paths as the h256-keyed secure build, so both entry
// points must yield the identical root. This pins the var-key path to the same canonical build
// core as the secure path (the short-key OP receipts-root usage relies on that same core).
BOOST_AUTO_TEST_CASE(VarKey32ByteMatchesSecureBuild)
{
    // Distinct 32-byte keys (each byte nonzero to stay distinct across the whole width), values
    // arbitrary. Sorted ascending — the var-key contract.
    std::vector<std::pair<bcos::bytes, bcos::bytes>> entries;
    std::vector<std::pair<bcos::h256, bcos::bytes>> asH256;
    for (uint32_t i = 0; i < 5; ++i)
    {
        bcos::bytes key(bcos::h256::SIZE);
        bcos::h256 hkey{};
        for (size_t j = 0; j < bcos::h256::SIZE; ++j)
        {
            key[j] = static_cast<bcos::byte>(i + 1);
            hkey.data()[j] = key[j];
        }
        bcos::bytes value{static_cast<bcos::byte>(0x10 + i)};
        entries.emplace_back(key, value);
        asH256.emplace_back(hkey, value);
    }
    std::sort(entries.begin(), entries.end(),
        [](auto const& a, auto const& b) { return a.first < b.first; });

    TrieBuildResult const varResult = computeTrieRootVarKey(entries);
    TrieBuildResult const secureResult = computeTrieRoot(ctrSortedMap(asH256));
    BOOST_CHECK_EQUAL(varResult.root, secureResult.root);
    BOOST_CHECK(varResult.newNodes == secureResult.newNodes);

    // Cross-check against the reference oracle (referenceRoot's refInsert is insertion-ordered;
    // feed the same ascending order the secure map uses).
    std::sort(asH256.begin(), asH256.end(),
        [](auto const& a, auto const& b) { return a.first < b.first; });
    BOOST_CHECK_EQUAL(secureResult.root, referenceRoot(asH256));
}

// Short (non-32-byte) keys: the build runs and yields a NON-empty root different from the empty
// trie — the receipts-root usage relies on this. (No padded-h256 equivalence exists for short
// keys: their nibble paths are shorter than 64, so the MPT shape differs by construction.)
BOOST_AUTO_TEST_CASE(VarKeyShortKeysBuildToNonEmptyRoot)
{
    std::vector<std::pair<bcos::bytes, bcos::bytes>> entries{
        {bcos::bytes{0x01}, bcos::bytes{0xbb}},
        {bcos::bytes{0x02}, bcos::bytes{0xcc}},
        {bcos::bytes{0x80}, bcos::bytes{0xaa}},
    };
    TrieBuildResult const result = computeTrieRootVarKey(entries);
    BOOST_CHECK(result.root != emptyRootHash());
    BOOST_CHECK(!result.newNodes.empty());
}

// The prefix-free/distinct caller contract is ENFORCED, not just documented: a key that is a
// prefix of another (or a duplicate) terminates inside a branch node and would silently
// produce a malformed trie — the W6 shape. The build must throw instead of emitting a root.
BOOST_AUTO_TEST_CASE(VarKeyRejectsPrefixAndDuplicateKeys)
{
    std::vector<std::pair<bcos::bytes, bcos::bytes>> prefixEntries{
        {bcos::bytes{0x01}, bcos::bytes{0xaa}},
        {bcos::bytes{0x01, 0x02}, bcos::bytes{0xbb}},
    };
    BOOST_CHECK_THROW(computeTrieRootVarKey(prefixEntries), MPTInvariantViolation);

    std::vector<std::pair<bcos::bytes, bcos::bytes>> duplicateEntries{
        {bcos::bytes{0x03}, bcos::bytes{0xaa}},
        {bcos::bytes{0x03}, bcos::bytes{0xbb}},
    };
    BOOST_CHECK_THROW(computeTrieRootVarKey(duplicateEntries), MPTInvariantViolation);
}

// Regression for the W6 L2 divergence (isthmus_big_block_130tx, 131 tx): the OP callers
// (computeOpTxRoot / sealOpBlock) pass index-order keys — rlp(0)=0x80, rlp(1..127)=0x01..0x7f,
// rlp(128..)=0x8180.. — which is NOT byte-sorted. At >= 128 entries the first key 0x80 shares its
// leading 0x8 nibble with the 2-byte keys, so the first/last common-prefix shortcut spanned
// [8] while the middle entries (0x01..0x7f) start with 0-7, producing a malformed extension node.
// computeTrieRootVarKey must be order-independent (defensive sort): unsorted input must yield the
// same root as sorted input.
BOOST_AUTO_TEST_CASE(VarKeyUnorderedManyKeysOrderIndependent)
{
    // 131 keys in INDEX order (the OP caller's order): 0x80, 0x01..0x7f, 0x8180..0x8182.
    std::vector<std::pair<bcos::bytes, bcos::bytes>> indexOrder;
    indexOrder.reserve(131);
    indexOrder.emplace_back(bcos::bytes{0x80}, bcos::bytes{0xaa});  // index 0
    for (uint32_t i = 1; i < 128; ++i)
    {
        indexOrder.emplace_back(
            bcos::bytes{static_cast<bcos::byte>(i)}, bcos::bytes{0xbb});  // 0x01..0x7f
    }
    for (uint32_t i = 128; i < 131; ++i)
    {
        indexOrder.emplace_back(
            bcos::bytes{0x81, static_cast<bcos::byte>(i)}, bcos::bytes{0xcc});  // 0x8180..0x8182
    }
    // Sanity: this input really is unsorted (0x80 must not sort first).
    auto sorted = indexOrder;
    std::sort(sorted.begin(), sorted.end(),
        [](auto const& a, auto const& b) { return a.first < b.first; });
    BOOST_CHECK(!std::equal(indexOrder.begin(), indexOrder.end(), sorted.begin(),
        [](auto const& a, auto const& b) { return a.first == b.first; }));

    TrieBuildResult const unsortedResult = computeTrieRootVarKey(indexOrder);
    TrieBuildResult const sortedResult = computeTrieRootVarKey(sorted);
    BOOST_CHECK_EQUAL(unsortedResult.root, sortedResult.root);
    BOOST_CHECK(unsortedResult.root != emptyRootHash());
    BOOST_CHECK(!unsortedResult.newNodes.empty());
}

// Empty var-key input commits to the canonical empty-trie root.
BOOST_AUTO_TEST_CASE(VarKeyEmptyIsEmptyRoot)
{
    TrieBuildResult const result = computeTrieRootVarKey({});
    BOOST_CHECK_EQUAL(result.root, emptyRootHash());
    BOOST_CHECK(result.newNodes.empty());
}

// The stateless core must agree with the independent reference oracle AND with the commitTrie
// from-empty entry point — same root and a byte-identical produced node set — across several
// batch sizes.
BOOST_AUTO_TEST_CASE(MatchesReferenceAndStatefulCommit)
{
    for (size_t size : {size_t{1}, size_t{2}, size_t{5}, size_t{20}, size_t{100}})
    {
        auto const kvs = ctrRandomKvs(size, /*seed=*/0xABC + static_cast<uint32_t>(size));
        bcos::h256 const expected = referenceRoot(kvs);

        auto const sorted = ctrSortedMap(kvs);
        TrieBuildResult const result = computeTrieRoot(sorted);
        BOOST_CHECK_EQUAL(result.root, expected);

        bcos::storage2::memory_storage::MemoryStorage<bcos::h256, bcos::bytes> storage;
        auto commitResult = seedTrieFlushed(storage, emptyRootHash(), sorted);
        BOOST_CHECK_EQUAL(result.root, commitResult.root);
        // unordered_map operator== is content-based: this is a byte-identical node-set check.
        BOOST_CHECK(result.newNodes == commitResult.newNodes);
    }
}

// An empty input commits to the canonical empty-trie root and produces no nodes.
BOOST_AUTO_TEST_CASE(EmptyInputIsEmptyRoot)
{
    std::map<bcos::h256, bcos::bytes> const empty;
    TrieBuildResult const result = computeTrieRoot(empty);
    BOOST_CHECK_EQUAL(result.root, emptyRootHash());
    BOOST_CHECK(result.newNodes.empty());
}

// computeTrieRoot owns no shared state, so building many independent tries concurrently must yield
// exactly the roots produced serially — the safety property coarse-grained parallelism relies on.
BOOST_AUTO_TEST_CASE(ConcurrentBuildsAreReentrant)
{
    constexpr size_t taskCount = 16;
    std::vector<std::map<bcos::h256, bcos::bytes>> inputs;
    std::vector<bcos::h256> expected;
    inputs.reserve(taskCount);
    expected.reserve(taskCount);
    for (size_t idx = 0; idx < taskCount; ++idx)
    {
        auto const kvs = ctrRandomKvs(50, /*seed=*/0xBEEF + static_cast<uint32_t>(idx));
        expected.push_back(referenceRoot(kvs));
        inputs.push_back(ctrSortedMap(kvs));
    }

    std::vector<bcos::h256> roots(taskCount);
    std::vector<std::thread> threads;
    threads.reserve(taskCount);
    for (size_t idx = 0; idx < taskCount; ++idx)
    {
        threads.emplace_back(
            [&inputs, &roots, idx]() { roots[idx] = computeTrieRoot(inputs[idx]).root; });
    }
    for (auto& thread : threads)
    {
        thread.join();
    }

    for (size_t idx = 0; idx < taskCount; ++idx)
    {
        BOOST_CHECK_EQUAL(roots[idx], expected[idx]);
    }
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
