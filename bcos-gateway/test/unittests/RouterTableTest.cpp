/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include <bcos-gateway/libp2p/router/RouterTableImpl.h>
#include <boost/test/unit_test.hpp>
#include <memory>

using namespace bcos::gateway;

namespace bcos::test
{
namespace
{
RouterTableEntryInterface::Ptr makeEntry(
    const std::string& dst, const std::string& nextHop, int32_t distance)
{
    auto entry = std::make_shared<RouterTableEntry>();
    entry->setDstNode(dst);
    entry->setNextHop(nextHop);
    entry->setDistance(distance);
    return entry;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(RouterTableTest)

BOOST_AUTO_TEST_CASE(entrySettersRoundTrip)
{
    RouterTableEntry entry;
    entry.setDstNode("dst");
    entry.setNextHop("hop");
    entry.setDistance(3);
    BOOST_CHECK_EQUAL(entry.dstNode(), "dst");
    BOOST_CHECK_EQUAL(entry.nextHop(), "hop");
    BOOST_CHECK_EQUAL(entry.distance(), 3);

    entry.incDistance(2);
    BOOST_CHECK_EQUAL(entry.distance(), 5);

    entry.clearNextHop();
    BOOST_CHECK(entry.nextHop().empty());
}

BOOST_AUTO_TEST_CASE(tableNodeIdAndEntries)
{
    RouterTable table;
    table.setNodeID("self");
    BOOST_CHECK_EQUAL(table.nodeID(), "self");

    table.updateDstNodeEntry("self", makeEntry("nodeA", "nodeA", 1));
    table.updateDstNodeEntry("self", makeEntry("nodeB", "nodeA", 2));

    BOOST_CHECK_EQUAL(table.routerEntries().size(), 2U);
    BOOST_CHECK_EQUAL(table.getNextHop("nodeA"), "nodeA");

    auto reachable = table.getAllReachableNode();
    BOOST_CHECK(reachable.find("nodeA") != reachable.end());
    BOOST_CHECK(reachable.find("nodeB") != reachable.end());
}

BOOST_AUTO_TEST_CASE(encodeDecodeRoundTrip)
{
    RouterTable table;
    table.setNodeID("self");
    table.updateDstNodeEntry("self", makeEntry("nodeA", "nodeA", 1));

    bcos::bytes encoded;
    table.encode(encoded);
    BOOST_REQUIRE(!encoded.empty());

    // encode/decode serializes the routing entries (the local nodeID is set
    // by the receiver, not carried in the table payload).
    RouterTable decoded(bcos::ref(encoded));
    BOOST_CHECK_EQUAL(decoded.routerEntries().size(), 1U);
    BOOST_CHECK(decoded.routerEntries().find("nodeA") != decoded.routerEntries().end());
}

BOOST_AUTO_TEST_CASE(eraseRemovesEntry)
{
    RouterTable table;
    table.setNodeID("self");
    table.updateDstNodeEntry("self", makeEntry("nodeA", "nodeA", 1));

    std::set<std::string> unreachable;
    table.erase(unreachable, "nodeA");
    auto reachable = table.getAllReachableNode();
    BOOST_CHECK(reachable.find("nodeA") == reachable.end());
}

BOOST_AUTO_TEST_CASE(factoryBuildsFromEncoded)
{
    RouterTable table;
    table.setNodeID("self");
    table.updateDstNodeEntry("self", makeEntry("nodeA", "nodeA", 1));
    bcos::bytes encoded;
    table.encode(encoded);

    RouterTableFactoryImpl factory;
    auto built = factory.createRouterTable(bcos::ref(encoded));
    BOOST_REQUIRE(built);
    BOOST_CHECK_EQUAL(built->routerEntries().size(), 1U);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
