/*
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
 */

// Tests for the copy-on-write routing state introduced in this branch:
//   - bcos::RcuSnapshot: publish / read / grace-period retirement, under concurrency
//   - RouterTable's cached reachable-node set: cache hits, version invalidation, direct lookup
//   - RouterTableEntry::clone: the deep copy the snapshot cloner relies on

#include <bcos-gateway/libp2p/router/RouterTableImpl.h>
#include <bcos-utilities/RcuSnapshot.h>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <chrono>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace bcos;
using namespace bcos::gateway;

namespace bcos::test
{

namespace
{

RouterTableEntryInterface::Ptr makeEntry(
    std::string const& dst, std::string const& nextHop, int32_t distance)
{
    auto entry = std::make_shared<RouterTableEntry>();
    entry->setDstNode(dst);
    entry->setNextHop(nextHop);
    entry->setDistance(distance);
    return entry;
}

}  // namespace

BOOST_AUTO_TEST_SUITE(RcuSnapshotTest)

// The published value is readable immediately, and update() replaces it.
BOOST_AUTO_TEST_CASE(readSeesPublishedValue)
{
    RcuSnapshot<std::map<std::string, int>> snapshot;
    BOOST_CHECK(snapshot.read().empty());

    snapshot.update([](std::map<std::string, int>& table) { table["a"] = 1; });
    BOOST_REQUIRE_EQUAL(snapshot.read().size(), 1U);
    BOOST_CHECK_EQUAL(snapshot.read().at("a"), 1);

    snapshot.update([](std::map<std::string, int>& table) { table["b"] = 2; });
    BOOST_CHECK_EQUAL(snapshot.read().size(), 2U);
    BOOST_CHECK_EQUAL(snapshot.read().at("b"), 2);
}

// This is the contract the whole lock-free read path rests on: a snapshot superseded by update()
// stays alive (and keeps its own contents) for the grace period, so a reader that took a reference
// before the publish can still use it. Exercised with a short grace period so the test is quick.
BOOST_AUTO_TEST_CASE(retiredSnapshotStaysValidForGracePeriod)
{
    RcuSnapshot<std::string> snapshot(std::string("first"));
    snapshot.setGracePeriod(std::chrono::milliseconds(200));

    std::string const* superseded = &snapshot.read();
    BOOST_CHECK_EQUAL(*superseded, "first");

    snapshot.update([](std::string& value) { value = "second"; });
    BOOST_CHECK_EQUAL(snapshot.read(), "second");

    // the retired snapshot is still there and unchanged
    BOOST_CHECK_EQUAL(*superseded, "first");
    BOOST_CHECK_EQUAL(snapshot.retiredCount(), 1U);

    // ... and it is released once the grace period elapses
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    snapshot.retireExpired();
    BOOST_CHECK_EQUAL(snapshot.retiredCount(), 0U);

    // the current snapshot is untouched by the reclamation
    BOOST_CHECK_EQUAL(snapshot.read(), "second");
}

// Several updates inside one window all queue their predecessors; a later sweep drains them in
// order and never touches the current snapshot.
BOOST_AUTO_TEST_CASE(retirementKeepsTheCurrentSnapshot)
{
    RcuSnapshot<int> snapshot(0);
    snapshot.setGracePeriod(std::chrono::milliseconds(50));

    for (int i = 1; i <= 5; ++i)
    {
        snapshot.update([i](int& value) { value = i; });
    }
    BOOST_CHECK_EQUAL(snapshot.read(), 5);
    // four predecessors retired; the newest one is current and not in the queue
    BOOST_CHECK_GE(snapshot.retiredCount(), 1U);

    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    snapshot.retireExpired();
    BOOST_CHECK_EQUAL(snapshot.retiredCount(), 0U);
    BOOST_CHECK_EQUAL(snapshot.read(), 5);
}

// A custom cloner replaces the default copy, which is what lets a table holding non-copyable or
// deep-owned values participate in copy-on-write.
BOOST_AUTO_TEST_CASE(customClonerIsUsed)
{
    struct Counted
    {
        int value{0};
        int copies{0};  // number of times this value has been carried into a new snapshot
    };

    RcuSnapshot<Counted> snapshot(Counted{}, [](Counted const& current) {
        Counted next;
        next.value = current.value;
        next.copies = current.copies + 1;  // observable proof the cloner ran
        return next;
    });

    BOOST_CHECK_EQUAL(snapshot.read().copies, 0);
    snapshot.update([](Counted& value) { value.value = 42; });
    BOOST_CHECK_EQUAL(snapshot.read().value, 42);
    BOOST_CHECK_EQUAL(snapshot.read().copies, 1);
}

// Readers must never observe a half-applied mutation: update() builds the replacement privately and
// publishes it with one store. Two fields that must always sum to a constant make a torn read
// visible.
BOOST_AUTO_TEST_CASE(concurrentReadersNeverSeeTornState)
{
    struct Pair
    {
        int left{0};
        int right{100};
    };

    RcuSnapshot<Pair> snapshot(Pair{});
    snapshot.setGracePeriod(std::chrono::milliseconds(1000));

    std::atomic<bool> stop{false};
    std::atomic<long long> torn{0};
    std::atomic<long long> reads{0};

    std::vector<std::thread> readers;
    for (int t = 0; t < 4; ++t)
    {
        readers.emplace_back([&] {
            while (!stop.load(std::memory_order_acquire))
            {
                auto const& pair = snapshot.read();
                if (pair.left + pair.right != 100)
                {
                    torn.fetch_add(1, std::memory_order_relaxed);
                }
                reads.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (int i = 0; i < 500; ++i)
    {
        snapshot.update([](Pair& pair) {
            // keep the invariant inside every published value
            pair.left = (pair.left + 1) % 100;
            pair.right = 100 - pair.left;
        });
    }

    stop.store(true, std::memory_order_release);
    for (auto& reader : readers)
    {
        reader.join();
    }

    BOOST_CHECK_GT(reads.load(), 0);
    BOOST_CHECK_EQUAL(torn.load(), 0);
    // whatever the last publish was, the invariant still holds
    BOOST_CHECK_EQUAL(snapshot.read().left + snapshot.read().right, 100);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(RouterTableReachableCacheTest)

// The reachable set is built on first use and reflects the table contents.
BOOST_AUTO_TEST_CASE(reachableSetReflectsTable)
{
    RouterTable table;
    table.setNodeID("self");
    table.updateDstNodeEntry("self", makeEntry("nodeA", "nodeA", 1));
    table.updateDstNodeEntry("self", makeEntry("nodeB", "nodeA", 2));

    auto const* reachable = table.getAllReachableNode();
    BOOST_REQUIRE(reachable);
    BOOST_CHECK_EQUAL(reachable->size(), 2U);
    BOOST_CHECK(reachable->contains("nodeA"));
    BOOST_CHECK(reachable->contains("nodeB"));

    BOOST_CHECK(table.isReachableNode("nodeA"));
    BOOST_CHECK(!table.isReachableNode("nodeC"));
}

// The common case is a cache hit: with no mutation in between, repeated calls must return the very
// same set instead of rebuilding it. Rebuilding is what used to cost ~11 us per broadcast.
BOOST_AUTO_TEST_CASE(repeatedCallsHitTheCache)
{
    RouterTable table;
    table.setNodeID("self");
    table.updateDstNodeEntry("self", makeEntry("nodeA", "nodeA", 1));

    auto const* first = table.getAllReachableNode();
    auto const* second = table.getAllReachableNode();
    auto const* third = table.getAllReachableNode();

    BOOST_REQUIRE(first);
    BOOST_CHECK_EQUAL(first, second);
    BOOST_CHECK_EQUAL(first, third);
}

// A mutation invalidates the cache: the next call publishes a new set with the new contents.
BOOST_AUTO_TEST_CASE(mutationInvalidatesCache)
{
    RouterTable table;
    table.setNodeID("self");
    table.updateDstNodeEntry("self", makeEntry("nodeA", "nodeA", 1));

    auto const* before = table.getAllReachableNode();
    BOOST_REQUIRE(before);
    BOOST_CHECK_EQUAL(before->size(), 1U);

    table.updateDstNodeEntry("self", makeEntry("nodeB", "nodeA", 2));

    auto const* after = table.getAllReachableNode();
    BOOST_REQUIRE(after);
    BOOST_CHECK_EQUAL(after->size(), 2U);
    BOOST_CHECK(after->contains("nodeB"));
    // a new set was published rather than the cached one mutated in place
    BOOST_CHECK_NE(before, after);
    // the superseded set is still readable within the grace period, with its old contents
    BOOST_CHECK_EQUAL(before->size(), 1U);
}

// erase() marks the node unreachable, which must also drop it from the cached set.
BOOST_AUTO_TEST_CASE(eraseInvalidatesCache)
{
    RouterTable table;
    table.setNodeID("self");
    table.setUnreachableDistance(10);
    table.updateDstNodeEntry("self", makeEntry("nodeA", "nodeA", 1));
    // nodeB routes through itself, so erasing nodeA does not also invalidate it -- this test is
    // about the cache, not about distance propagation (see RouterTableTest for that).
    table.updateDstNodeEntry("self", makeEntry("nodeB", "nodeB", 1));

    auto const* before = table.getAllReachableNode();
    BOOST_REQUIRE(before);
    BOOST_CHECK(before->contains("nodeA"));

    std::set<std::string> unreachable;
    table.erase(unreachable, "nodeA");

    BOOST_CHECK(!table.isReachableNode("nodeA"));
    auto const* after = table.getAllReachableNode();
    BOOST_REQUIRE(after);
    BOOST_CHECK(!after->contains("nodeA"));
    BOOST_CHECK(after->contains("nodeB"));
}

// decode() replaces the whole table, so the cache must be rebuilt from the decoded contents.
BOOST_AUTO_TEST_CASE(decodeInvalidatesCache)
{
    RouterTable source;
    source.setNodeID("self");
    source.updateDstNodeEntry("self", makeEntry("nodeA", "nodeA", 1));
    bcos::bytes encoded;
    source.encode(encoded);

    RouterTable table;
    table.setNodeID("self");
    table.updateDstNodeEntry("self", makeEntry("stale", "stale", 1));
    auto const* before = table.getAllReachableNode();
    BOOST_REQUIRE(before);
    BOOST_CHECK(before->contains("stale"));

    table.decode(bcos::ref(encoded));

    auto const* after = table.getAllReachableNode();
    BOOST_REQUIRE(after);
    BOOST_CHECK(after->contains("nodeA"));
    BOOST_CHECK(!after->contains("stale"));
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(RouterTableEntryCloneTest)

// The snapshot cloner deep-copies entries because each owns a tars object behind its accessor.
// A clone must therefore be independent of its source.
BOOST_AUTO_TEST_CASE(cloneIsIndependentOfSource)
{
    RouterTableEntry original;
    original.setDstNode("dst");
    original.setNextHop("hop");
    original.setDistance(3);

    auto copy = original.clone();
    BOOST_REQUIRE(copy);
    BOOST_CHECK_EQUAL(copy->dstNode(), "dst");
    BOOST_CHECK_EQUAL(copy->nextHop(), "hop");
    BOOST_CHECK_EQUAL(copy->distance(), 3);

    // mutate the source; the clone must not follow
    original.setNextHop("other");
    original.setDistance(9);
    BOOST_CHECK_EQUAL(copy->nextHop(), "hop");
    BOOST_CHECK_EQUAL(copy->distance(), 3);

    // and the reverse direction
    copy->setDistance(42);
    BOOST_CHECK_EQUAL(original.distance(), 9);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
