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
 * @file ImportedStoreIndexTest.cpp
 * @brief Height-index and liveness invariants of the staged import plane's store.
 */

#include <bcos-utilities/Common.h>
#include <boost/test/unit_test.hpp>
#include "engine/bcos-engine/ImportedStore.h"

namespace
{
bcos::h256 hashOf(char tag)
{
    bcos::h256 out{};
    out[0] = static_cast<bcos::byte>(tag);
    return out;
}

bcos::engine::ImportedBlock block(char tag, char parent, bcos::protocol::BlockNumber number)
{
    return bcos::engine::ImportedBlock{
        .hash = hashOf(tag), .parent = hashOf(parent), .number = number};
}
}  // namespace

BOOST_AUTO_TEST_SUITE(ImportedStoreIndexTest)

// put() and occupantAt() must share ONE occupancy notion. An allowed same-height
// coexistence (canonical occupant awaiting its own FCU) used to leave the height
// index naming the FIRST importer, so a child of the second sibling resolved
// occupantAt(parentHeight) to a block the engine no longer considers the occupant.
BOOST_AUTO_TEST_CASE(AllowedCoexistenceReKeysTheHeightIndex)
{
    bcos::engine::ImportedStore store;
    BOOST_CHECK(store.put(block('A', 0, 1)));
    // Second import at height 1 is allowed because the caller vouches the occupant
    // is canonical (ancestor-sibling shape, §4.3).
    BOOST_CHECK(store.put(block('B', 0, 1), /*occupantCanonical=*/true));
    BOOST_CHECK_EQUAL(*store.occupantAt(1), hashOf('B'));
    BOOST_CHECK(store.hasBlock(hashOf('A')));
    BOOST_CHECK(store.hasBlock(hashOf('B')));
}

// A switch at/below the old tip abandons BELOW-head old-branch blocks too: they are
// not ancestors of the new head and must be detached (and vacate their heights),
// otherwise the next legal import at those heights answers SYNCING forever.
BOOST_AUTO_TEST_CASE(BelowTipSwitchDetachesAbandonedBranchBelowHead)
{
    bcos::engine::ImportedStore store;
    // A(0) - B(1) - C(2); D(1) is a sibling of B.
    BOOST_CHECK(store.put(block('A', 0, 0)));
    BOOST_CHECK(store.put(block('B', 'A', 1)));
    BOOST_CHECK(store.put(block('C', 'B', 2)));
    BOOST_CHECK(store.put(block('D', 'A', 1), /*occupantCanonical=*/true));
    BOOST_CHECK_EQUAL(*store.occupantAt(1), hashOf('D'));

    // Switch-SetCanonical to D at height 1 (below the old tip 2).
    store.adoptCanonicalHead(1, hashOf('D'));

    // D is the head; A is its below-head ancestor — both stay live.
    BOOST_CHECK(store.hasBlock(hashOf('D')));
    BOOST_CHECK(store.hasBlock(hashOf('A')));
    BOOST_CHECK_EQUAL(*store.occupantAt(0), hashOf('A'));
    BOOST_CHECK_EQUAL(*store.occupantAt(1), hashOf('D'));
    // The abandoned B/C branch keeps its bodies (hash-addressable) but vacates the
    // heights and no longer blocks future imports via put()'s descendant guard.
    BOOST_CHECK(store.hasBlock(hashOf('B')));
    BOOST_CHECK(store.hasBlock(hashOf('C')));
    BOOST_CHECK(!store.occupantAt(2).has_value());

    // The next legal import at height 2 (child of D) is accepted: B's in-store child
    // C no longer guards the height.
    BOOST_CHECK(store.put(block('E', 'D', 2)));
    BOOST_CHECK_EQUAL(*store.occupantAt(2), hashOf('E'));
}

// A forward switch (new head above the old tip) keeps the old behaviour: everything
// on the new chain stays live, off-chain blocks above the head detach.
BOOST_AUTO_TEST_CASE(ForwardSwitchKeepsChainLiveness)
{
    bcos::engine::ImportedStore store;
    BOOST_CHECK(store.put(block('A', 0, 0)));
    BOOST_CHECK(store.put(block('B', 'A', 1)));
    BOOST_CHECK(store.put(block('C', 'B', 2)));
    store.adoptCanonicalHead(2, hashOf('C'));
    BOOST_CHECK_EQUAL(*store.occupantAt(0), hashOf('A'));
    BOOST_CHECK_EQUAL(*store.occupantAt(1), hashOf('B'));
    BOOST_CHECK_EQUAL(*store.occupantAt(2), hashOf('C'));
}

BOOST_AUTO_TEST_SUITE_END()
