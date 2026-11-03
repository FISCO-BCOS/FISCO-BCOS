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
 * @file KademliaTableTest.cpp
 * @brief Deterministic discv4 routing-table tests with injected node ids.
 * @date 2026/11/3
 */
#include <bcos-devp2p/disc/KademliaTable.h>
#include <bcos-devp2p/rlpx/Crypto.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>
#include <chrono>

using namespace bcos;
using namespace bcos::devp2p;
using namespace bcos::devp2p::disc;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(KademliaTableTest)

namespace
{
// An id whose XOR distance from `_self` has its highest set bit at `_logDistance`
// (1..256), i.e. lands in bucket `_logDistance - 1`. `_salt` varies the lower bits.
h256 idAtDistance(h256 const& _self, int _logDistance, uint64_t _salt)
{
    h256 id = _self;
    int bit = _logDistance - 1;  // bit index from the least significant end
    size_t byteIndex = h256::SIZE - 1 - static_cast<size_t>(bit / 8);
    id[byteIndex] ^= static_cast<uint8_t>(1U << (bit % 8));
    // Scramble the bits below `bit` deterministically (never touching higher bits).
    for (int lower = 0; lower < bit && lower < 64; ++lower)
    {
        if ((_salt >> lower) & 1U)
        {
            size_t idx = h256::SIZE - 1 - static_cast<size_t>(lower / 8);
            id[idx] ^= static_cast<uint8_t>(1U << (lower % 8));
        }
    }
    return id;
}

NodeEntry entryWithId(h256 const& _id, uint16_t _port = 30303)
{
    NodeEntry entry;
    entry.id = _id;
    entry.publicKey = bcos::bytes(64, static_cast<bcos::byte>(_port & 0xff));
    entry.ip = {10, 0, 0, 1};
    entry.udp = _port;
    entry.tcp = _port;
    return entry;
}
}  // namespace

BOOST_AUTO_TEST_CASE(distanceAndNodeId)
{
    h256 a("0x0000000000000000000000000000000000000000000000000000000000000000");
    BOOST_CHECK_EQUAL(KademliaTable::logDistance(a, a), 0);
    h256 one("0x0000000000000000000000000000000000000000000000000000000000000001");
    BOOST_CHECK_EQUAL(KademliaTable::logDistance(a, one), 1);
    h256 top("0x8000000000000000000000000000000000000000000000000000000000000000");
    BOOST_CHECK_EQUAL(KademliaTable::logDistance(a, top), 256);
    h256 mid("0x0000000000000000000000000000000100000000000000000000000000000000");
    BOOST_CHECK_EQUAL(KademliaTable::logDistance(a, mid), 129);
    BOOST_CHECK(KademliaTable::xorDistance(one, top) ==
                h256("0x8000000000000000000000000000000000000000000000000000000000000001"));
    // Node id = keccak256(uncompressed public key): the enr.md vector's key/id pair.
    rlpx::EccKeyPair key(
        fromHex(std::string("b71c71a67e1177ad4e901695e1b4b9ee17ae16c6668d313eac2f96dbcda3f291")));
    BOOST_CHECK_EQUAL(KademliaTable::nodeIdOf(bcos::ref(key.publicKey())).hex(),
        "a448f24c6d18e575453db13171562b71999873db5b286df957af199ec94617f7");
    // idAtDistance helper lands in the intended bucket.
    h256 self("0x1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
    for (int d : {1, 7, 8, 9, 100, 255, 256})
    {
        BOOST_CHECK_EQUAL(KademliaTable::logDistance(self, idAtDistance(self, d, 0xabcdef)), d);
    }
}

BOOST_AUTO_TEST_CASE(bucketFillAndReplacementCache)
{
    h256 self("0x1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
    KademliaTable table(self);
    BOOST_CHECK(table.add(entryWithId(self)) == KademliaTable::AddResult::Self);
    BOOST_CHECK_EQUAL(table.size(), 0u);

    // 16 nodes fill bucket 199 (log distance 200); the 17th..26th go to the replacement
    // cache; the 27th evicts the oldest replacement.
    std::vector<h256> ids;
    for (uint64_t i = 0; i < 27; ++i)
    {
        ids.push_back(idAtDistance(self, 200, 1000 + i));
    }
    for (size_t i = 0; i < 16; ++i)
    {
        BOOST_CHECK(table.add(entryWithId(ids[i], static_cast<uint16_t>(i + 1))) ==
                    KademliaTable::AddResult::Added);
    }
    BOOST_CHECK_EQUAL(table.size(), 16u);
    BOOST_CHECK_EQUAL(table.bucketSize(199), 16u);
    for (size_t i = 16; i < 27; ++i)
    {
        BOOST_CHECK(table.add(entryWithId(ids[i], static_cast<uint16_t>(i + 1))) ==
                    KademliaTable::AddResult::Replacement);
    }
    BOOST_CHECK_EQUAL(table.size(), 16u);
    BOOST_CHECK(table.find(ids[16]) == nullptr);  // evicted from the cache (size 10)
    BOOST_CHECK(table.find(ids[17]) != nullptr);  // still cached
    BOOST_CHECK(table.find(ids[26]) != nullptr);
    // Re-adding a present node updates in place.
    auto refreshed = entryWithId(ids[0], 4000);
    refreshed.enrSeq = 9;
    BOOST_CHECK(table.add(refreshed) == KademliaTable::AddResult::Updated);
    BOOST_CHECK_EQUAL(table.find(ids[0])->udp, 4000);
    BOOST_CHECK_EQUAL(table.find(ids[0])->enrSeq, 9u);
    BOOST_CHECK_EQUAL(table.size(), 16u);

    // Removing a bucket entry promotes the newest replacement (ids[26]).
    BOOST_CHECK(table.remove(ids[3]));
    BOOST_CHECK_EQUAL(table.size(), 16u);
    BOOST_CHECK(table.find(ids[3]) == nullptr);
    auto entries = table.all();
    BOOST_CHECK(std::any_of(
        entries.begin(), entries.end(), [&](NodeEntry const& e) { return e.id == ids[26]; }));
    BOOST_CHECK(!table.remove(ids[3]));
    BOOST_CHECK(!table.remove(self));

    // Least recently seen is the bucket head; touch moves to the tail.
    auto candidate = table.revalidationCandidate(199);
    BOOST_REQUIRE(candidate.has_value());
    BOOST_CHECK(*candidate == ids[0]);
    table.touch(ids[0]);
    candidate = table.revalidationCandidate(199);
    BOOST_REQUIRE(candidate.has_value());
    BOOST_CHECK(*candidate == ids[1]);
    // A random starting bucket still finds the only non-empty bucket.
    BOOST_CHECK(table.revalidationCandidate(5).has_value());
}

BOOST_AUTO_TEST_CASE(closestOrdersByXorDistance)
{
    h256 self("0x0000000000000000000000000000000000000000000000000000000000000000");
    KademliaTable table(self);
    // Ids 1..40 (as integers): distance to target 0x20 (=32) is id XOR 32.
    for (uint64_t i = 1; i <= 40; ++i)
    {
        h256 id;
        id[31] = static_cast<uint8_t>(i);
        table.add(entryWithId(id, static_cast<uint16_t>(i)));
    }
    BOOST_CHECK_EQUAL(table.size(), 40u);
    h256 target;
    target[31] = 32;
    auto closest = table.closest(target, 5);
    BOOST_REQUIRE_EQUAL(closest.size(), 5u);
    // Closest to 32: 32 (dist 0), 33 (1), 34 (2), 35 (3), 36 (4).
    for (size_t i = 0; i < 5; ++i)
    {
        BOOST_CHECK_EQUAL(static_cast<unsigned>(closest[i].id[31]), 32u + i);
    }
    BOOST_CHECK_EQUAL(table.closest(target, 100).size(), 40u);
    KademliaTable empty(self);
    BOOST_CHECK(empty.closest(target, 3).empty());
    BOOST_CHECK(!empty.revalidationCandidate(0).has_value());
}

BOOST_AUTO_TEST_CASE(bondBookkeeping)
{
    using Clock = NodeEntry::Clock;
    NodeEntry entry;
    auto now = Clock::now();
    BOOST_CHECK(!entry.bonded(now));
    BOOST_CHECK(!entry.hasProofOfUs(now));
    entry.lastPongReceived = now - std::chrono::hours(11);
    BOOST_CHECK(entry.bonded(now));
    entry.lastPongReceived = now - std::chrono::hours(13);
    BOOST_CHECK(!entry.bonded(now));
    entry.lastPingReceived = now - std::chrono::minutes(1);
    BOOST_CHECK(entry.hasProofOfUs(now));

    // add() keeps the later timestamps and the newer record.
    h256 self;
    self[0] = 1;
    KademliaTable table(self);
    h256 id;
    id[31] = 7;
    auto first = entryWithId(id);
    first.lastPongReceived = now - std::chrono::hours(1);
    table.add(first);
    auto second = entryWithId(id);
    second.lastPongReceived = now - std::chrono::hours(2);  // older: ignored
    second.lastPingReceived = now;
    table.add(second);
    BOOST_CHECK(table.find(id)->lastPongReceived == first.lastPongReceived);
    BOOST_CHECK(table.find(id)->lastPingReceived == now);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
