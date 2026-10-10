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

// Field-by-field on a value-initialized block: an aggregate initializer naming only the
// three ancestry fields leaves the eight payload members uninitialized, which gcc's
// -Wmissing-field-initializers (CI is -Werror) rejects. The other members are irrelevant
// to the height-index/liveness invariants under test.
bcos::engine::ImportedBlock block(char tag, char parent, bcos::protocol::BlockNumber number)
{
    bcos::engine::ImportedBlock out{};
    out.hash = hashOf(tag);
    out.parent = hashOf(parent);
    out.number = number;
    return out;
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

// M7: a sparse store (intermediate body never staged) must not detach a live
// canonical ancestor: block 100 is a live ancestor of 102 while 101 was never
// imported, so the walk cannot decide ancestry — and must NOT detach.
BOOST_AUTO_TEST_CASE(SparseStoreKeepsLiveAncestor)
{
    bcos::engine::ImportedStore store;
    BOOST_CHECK(store.put(block('A', 0, 100)));
    // Sparse: 101 is skipped — only 100 and 102 are staged, with 102's parent = 101.
    BOOST_CHECK(store.put(block('C', 'B', 102)));
    store.adoptCanonicalHead(102, hashOf('C'));
    // A is a live canonical ancestor (the walk from C cannot reach it, so the store
    // must NOT detach it).
    BOOST_CHECK(store.hasBlock(hashOf('A')));  // body stays
    // The canonical height index keeps A at 100 (not detached => not erased).
    BOOST_CHECK_EQUAL(*store.occupantAt(100), hashOf('A'));
}

// M8: a switch-away-and-back must re-index the re-livened block — the old
// erase-only fix-up left occupantAt answering nullopt for a live block.
BOOST_AUTO_TEST_CASE(SwitchAwayAndBackReindexesLiveOccupant)
{
    bcos::engine::ImportedStore store;
    BOOST_CHECK(store.put(block('A', 0, 1)));
    BOOST_CHECK(store.put(block('B', 'A', 2)));
    // Switch to a sibling chain: A' at 1 (canonical occupant shape).
    BOOST_CHECK(store.put(block('D', 0, 1), /*occupantCanonical=*/true));
    store.adoptCanonicalHead(1, hashOf('D'));
    BOOST_CHECK_EQUAL(*store.occupantAt(1), hashOf('D'));
    BOOST_CHECK(!store.occupantAt(2).has_value());  // B detached, height 2 vacated
    // Switch back to the original chain: B re-livened, A stays live at 1.
    store.adoptCanonicalHead(2, hashOf('B'));
    BOOST_CHECK_EQUAL(*store.occupantAt(1), hashOf('A'));
    BOOST_CHECK_EQUAL(*store.occupantAt(2), hashOf('B'));
}

// R1: a parent cycle (A.parent=Q, Q.parent=A) below a sparsely-linked head makes
// the ancestry walk UNDECIDABLE, same as a sparse store — the store must not detach.
// descendsFromWalkable's guard exhaustion once answered true (fail-open), letting the
// below-head pass detach a block whose liveness it could not decide; the walk now
// returns false on exhaustion, mirroring descendsFrom.
BOOST_AUTO_TEST_CASE(CyclicParentWalkKeepsUndecidableAncestor)
{
    bcos::engine::ImportedStore store;
    // A and Q form a 2-cycle; C's parent was never staged (sparse link to the cycle).
    BOOST_CHECK(store.put(block('A', 'Q', 100)));
    BOOST_CHECK(store.put(block('Q', 'A', 101)));
    BOOST_CHECK(store.put(block('C', 'B', 102)));
    store.adoptCanonicalHead(102, hashOf('C'));
    // A's liveness cannot be decided (its own walk cycles; the head walk hits the
    // unstaged B) — the store must keep it: body hash-addressable, height kept.
    auto const gotA = store.get(hashOf('A'));
    BOOST_REQUIRE(gotA.has_value());
    BOOST_CHECK(!gotA->detached);
    BOOST_CHECK_EQUAL(*store.occupantAt(100), hashOf('A'));
}

// Rebuild arbitration must be deterministic: a canonical ancestor always wins its
// height over a live off-lineage sibling (whose sparse ancestry keeps it stored but
// must not let it occupy the canonical chain's slot).
BOOST_AUTO_TEST_CASE(CanonicalAncestorWinsHeightOverLiveSibling)
{
    bcos::engine::ImportedStore store;
    BOOST_CHECK(store.put(block('A', 0, 100)));
    BOOST_CHECK(store.put(block('B', 'A', 101)));
    BOOST_CHECK(store.put(block('C', 'B', 102)));
    // Sibling at the same height as A, with an unstored parent: stays live (sparse
    // ancestry is undecidable, so the store must not detach it) — but must not take
    // height 100 from the canonical ancestor. Same-height coexistence needs the caller
    // vouching the existing occupant is canonical (§4.3), like every sibling import.
    BOOST_CHECK(store.put(block('S', 0, 100), /*occupantCanonical=*/true));
    store.adoptCanonicalHead(102, hashOf('C'));
    BOOST_CHECK_EQUAL(*store.occupantAt(100), hashOf('A'));
    BOOST_CHECK_EQUAL(*store.occupantAt(102), hashOf('C'));
    BOOST_CHECK(store.hasBlock(hashOf('S')));  // still hash-addressable for re-import
}

// The detach must actually FIRE when ancestry is decidable: an old-branch block at a
// height the new chain occupies is detached and its height re-keyed to the new chain's
// block. (The walk from the head passes the candidate's height via the sibling, so the
// answer is decided; contrast SparseStoreKeepsLiveAncestor, where the walk can't reach
// the candidate's height and nothing detaches.)
BOOST_AUTO_TEST_CASE(BelowHeadOldBranchBlockDetachedWhenDecided)
{
    bcos::engine::ImportedStore store;
    BOOST_CHECK(store.put(block('A', 0, 100)));
    BOOST_CHECK(store.put(block('D', 0, 100), /*occupantCanonical=*/true));  // sibling at 100
    BOOST_CHECK(store.put(block('B', 'D', 101)));
    BOOST_CHECK(store.put(block('C', 'B', 102)));
    store.adoptCanonicalHead(102, hashOf('C'));
    BOOST_CHECK_EQUAL(*store.occupantAt(100), hashOf('D'));  // the sibling, not A
    BOOST_CHECK(store.hasBlock(hashOf('A')));                // body stays for re-import
}

BOOST_AUTO_TEST_SUITE_END()
