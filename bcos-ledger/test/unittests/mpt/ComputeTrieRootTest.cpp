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
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/test/unit_test.hpp>
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
