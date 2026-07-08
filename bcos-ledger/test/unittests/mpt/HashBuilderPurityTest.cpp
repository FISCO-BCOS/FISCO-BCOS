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
 * @file HashBuilderPurityTest.cpp
 * @brief Determinism gate: identical inputs yield a byte-identical root + node set (spec §5.6)
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
#include <algorithm>
#include <random>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bcos::ledger::mpt::test
{

BOOST_AUTO_TEST_SUITE(HashBuilderPuritySuite)

namespace
{
// Distinct random h256 keys + small random values from a fixed seed. Named uniquely (purity*) to
// avoid an anonymous-namespace ODR clash with HashBuilderTest under UNITY_BUILD.
std::vector<std::pair<bcos::h256, bcos::bytes>> purityKvs(size_t count, uint32_t seed)
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

// Spec §5.6 hard requirement: the same input set, fed in different (shuffled) orders across many
// runs, MUST produce a byte-identical 32-byte root AND a byte-identical new-node set. This is the
// precondition for crash-replay convergence — two nodes that replay the same change-set in any
// order must land on the same state root and persist the same nodes. Do NOT reduce 1000.
BOOST_AUTO_TEST_CASE(SameInputProducesByteIdenticalRootOver1000Runs)
{
    auto const entries = purityKvs(100, /*seed=*/0x9176AB);

    bcos::h256 referenceRootHash{};
    std::unordered_map<bcos::h256, bcos::bytes> referenceNodes;

    for (uint32_t run = 0; run < 1000; ++run)
    {
        auto shuffled = entries;  // fresh copy each run
        std::mt19937 orderRng{run};
        std::shuffle(shuffled.begin(), shuffled.end(), orderRng);

        bcos::storage2::memory_storage::MemoryStorage<bcos::h256, bcos::bytes> storage;
        HashBuilder hb(storage, emptyRootHash());
        for (auto const& [key, value] : shuffled)
        {
            bcos::task::syncWait(hb.put(key, value));
        }
        auto const root = bcos::task::syncWait(hb.commit());
        auto nodes = hb.drainNewNodes();

        if (run == 0)
        {
            referenceRootHash = root;
            referenceNodes = std::move(nodes);
        }
        else
        {
            // unordered_map operator== is content/order-independent: this is a byte-identical
            // check of the entire produced node set, not just the root.
            BOOST_REQUIRE_MESSAGE(root == referenceRootHash,
                "root drift at run " << run << " got=" << root.hex()
                                     << " expected=" << referenceRootHash.hex());
            BOOST_REQUIRE_MESSAGE(nodes == referenceNodes, "node-set drift at run " << run);
        }
    }
}

// An empty change-set commits to the canonical empty-trie root every time.
BOOST_AUTO_TEST_CASE(EmptyInputDeterministicallyEmptyRoot)
{
    for (int run = 0; run < 8; ++run)
    {
        bcos::storage2::memory_storage::MemoryStorage<bcos::h256, bcos::bytes> storage;
        HashBuilder hb(storage, emptyRootHash());
        auto const root = bcos::task::syncWait(hb.commit());
        BOOST_CHECK_EQUAL(root, emptyRootHash());
        BOOST_CHECK(hb.drainNewNodes().empty());
    }
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
