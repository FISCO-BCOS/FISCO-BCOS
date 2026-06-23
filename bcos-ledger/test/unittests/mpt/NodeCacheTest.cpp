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
 * @file NodeCacheTest.cpp
 * @brief Unit tests for NodeCache (LRU+CONCURRENT MPT node cache, spec §5.1)
 */

#include <bcos-ledger/mpt/NodeCache.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/test/unit_test.hpp>
#include <array>
#include <thread>

namespace bcos::ledger::mpt::test
{

namespace
{
/// A key whose low byte is @p tag and all other bytes zero.
bcos::h256 makeKey(bcos::byte tag)
{
    bcos::h256 key{};  // zero-initialised
    key.data()[0] = tag;
    return key;
}

/// Mirror of MemoryStorage's bucket sizing for a CONCURRENT storage created with
/// buckets==0: getBucketSize() == hardware_concurrency() * 2 + 1.
unsigned bucketCount()
{
    return std::thread::hardware_concurrency() * 2 + 1;
}

/// Mirror of MemoryStorage::getBucketIndex: std::hash<h256>(key) % numBuckets.
size_t bucketIndexOf(bcos::h256 const& key, unsigned numBuckets)
{
    return std::hash<bcos::h256>{}(key) % numBuckets;
}

/// Find @p howMany distinct keys that all land in the same CONCURRENT bucket,
/// so per-bucket LRU eviction is exercised deterministically regardless of the
/// host CPU count.
std::vector<bcos::h256> sameBucketKeys(size_t howMany)
{
    unsigned const numBuckets = bucketCount();
    std::vector<bcos::h256> result;
    for (int target = 0; target < (int)numBuckets && result.empty(); ++target)
    {
        std::vector<bcos::h256> candidates;
        for (int tag = 1; tag < 256 && candidates.size() < howMany; ++tag)
        {
            auto key = makeKey(static_cast<bcos::byte>(tag));
            if (bucketIndexOf(key, numBuckets) == (size_t)target)
            {
                candidates.push_back(key);
            }
        }
        if (candidates.size() >= howMany)
        {
            result = std::move(candidates);
        }
    }
    return result;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(NodeCacheSuite)

// ---------------------------------------------------------------------------
// Test 1: a value put under a key is returned verbatim by get.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(PutGetRoundTrip)
{
    NodeCache cache;
    auto key = makeKey(0x11);
    bcos::bytes value{0xde, 0xad, 0xbe, 0xef};

    bcos::task::syncWait(cache.put(key, value));
    auto got = bcos::task::syncWait(cache.get(key));

    BOOST_REQUIRE(got.has_value());
    BOOST_CHECK(*got == value);
}

// ---------------------------------------------------------------------------
// Test 2: a get for a key that was never put returns nullopt.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(MissReturnsNullopt)
{
    NodeCache cache;
    auto present = makeKey(0x01);
    auto absent = makeKey(0x02);

    bcos::task::syncWait(cache.put(present, bcos::bytes{0xaa}));
    auto got = bcos::task::syncWait(cache.get(absent));

    BOOST_CHECK(!got.has_value());
}

// ---------------------------------------------------------------------------
// Test 3: when the byte budget is exceeded, the least-recently-used entry is
// evicted. Capacity is measured in bytes (getSize(key)+getSize(value)); each
// entry here is 32 (h256) + 1 (value) = 33 bytes. With a 50-byte budget only one
// entry fits, so inserting three keys (oldest first) leaves only the newest.
//
// Eviction is per-bucket, so the three keys are chosen to share a single bucket;
// otherwise they would scatter across the CONCURRENT shards and never collide.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(LRUEvictionWhenOverCapacity)
{
    auto keys = sameBucketKeys(3);
    BOOST_REQUIRE_EQUAL(keys.size(), 3u);

    NodeCache cache(/*capacity=*/50);

    // Insert oldest -> newest.
    bcos::task::syncWait(cache.put(keys[0], bcos::bytes{0x10}));
    bcos::task::syncWait(cache.put(keys[1], bcos::bytes{0x20}));
    bcos::task::syncWait(cache.put(keys[2], bcos::bytes{0x30}));

    auto first = bcos::task::syncWait(cache.get(keys[0]));
    auto second = bcos::task::syncWait(cache.get(keys[1]));
    auto third = bcos::task::syncWait(cache.get(keys[2]));

    // Oldest two evicted, newest survives.
    BOOST_CHECK(!first.has_value());
    BOOST_CHECK(!second.has_value());
    BOOST_REQUIRE(third.has_value());
    BOOST_CHECK_EQUAL((*third)[0], 0x30);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
