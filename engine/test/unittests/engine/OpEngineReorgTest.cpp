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
 * @file OpEngineReorgTest.cpp
 * @brief Unfinalized window on the OP Engine lane: D1 §15 rows 1-8 and the §12.3 boundaries.
 *
 * Real OpScheduler<MLS> over in-memory backends, every block produced through the engine's own
 * build path (FCU+attrs → getPayload → newPayload) so no commitment is hand-computed. The chain
 * follows D1 §2.1: bal(0xAA) is 10 at B1, 20 at B2a, 15 at B2b (mint deposits of 10/10/5).
 */

#include "OpReorgFixture.h"
#include <boost/test/unit_test.hpp>


BOOST_AUTO_TEST_SUITE(OpEngineReorgTest)

using namespace op_engine_reorg;

// D1 §15 row 1 — sibling at the tip via the derivation reorg.
BOOST_AUTO_TEST_CASE(row1_tip_sibling_via_derivation_reorg)
{
    ReorgFixture f;
    auto c = setupB1B2a(f);
    auto s = deriveB2b(f, c);

    BOOST_CHECK_EQUAL(f.trackedHead().value_or(bcos::h256{}).hex(), s.h2b.hex());
    BOOST_CHECK_EQUAL(f.balanceAt(s.h2b), bcos::u256(15));  // latest follows the head
    BOOST_CHECK_EQUAL(f.balanceAt(c.h2a), bcos::u256(20));  // the sibling keeps its own state
    BOOST_CHECK_EQUAL(f.backendBalance(), bcos::u256(10));  // nothing reached the backend
    BOOST_CHECK_EQUAL(f.backendNumber(), 1);
    BOOST_CHECK(f.inWindow(c.h2a));
    BOOST_CHECK(f.inWindow(s.h2b));
    BOOST_CHECK_EQUAL(f.opDelegate->windowSize(), 2U);
    // Both siblings answer for height 2 on their own chain.
    BOOST_CHECK_EQUAL(f.onChainAt(s.h2b, 2).value_or(bcos::h256{}).hex(), s.h2b.hex());
    BOOST_CHECK_EQUAL(f.onChainAt(c.h2a, 2).value_or(bcos::h256{}).hex(), c.h2a.hex());
    BOOST_CHECK_EQUAL(f.onChainAt(s.h2b, 1).value_or(bcos::h256{}).hex(), c.h1.hex());
    // Header of an unfinalized block is served from its window entry.
    auto header = f.service->executedHeader(s.h2b);
    BOOST_REQUIRE(header);
    BOOST_CHECK_EQUAL(header->number(), 2);
    BOOST_CHECK_EQUAL(header->stateRoot().hex(), s.b2b.executionPayload.stateRoot.hex());

    // Negative control: a finalized block that is NOT on the head's chain (B2b under head
    // B2a) is -38002, and the tracker check runs BEFORE finalizeUpTo — nothing is merged,
    // nothing is pruned, the tracker keeps the previous head.
    BOOST_CHECK_THROW(f.fcu(f.fc(c.h2a, c.h1, s.h2b)), bcos::engine::InvalidForkchoiceState);
    BOOST_CHECK_EQUAL(f.backendNumber(), 1);
    BOOST_CHECK_EQUAL(f.opDelegate->finalizedNumber(), 1);
    BOOST_CHECK(f.inWindow(c.h2a));
    BOOST_CHECK(f.inWindow(s.h2b));
    BOOST_CHECK_EQUAL(f.trackedHead().value_or(bcos::h256{}).hex(), s.h2b.hex());
    // Same for safe off the head's chain.
    BOOST_CHECK_THROW(f.fcu(f.fc(c.h2a, s.h2b, c.h1)), bcos::engine::InvalidForkchoiceState);
    BOOST_CHECK_EQUAL(f.trackedHead().value_or(bcos::h256{}).hex(), s.h2b.hex());
}

// Rows 2, 3, 4 (+ the prune assertion): depth > 1, rewind to a non-tip ancestor, finalize
// past the side branch.
BOOST_AUTO_TEST_CASE(rows2_3_4_depth_rewind_and_finalize_past_side_branch)
{
    ReorgFixture f;
    auto c = setupB1B2a(f);
    auto s = deriveB2b(f, c);

    // Row 2: B3b, B4b on top of B2b (each mints 1 → 16, 17).
    auto b3b = f.build(f.fc(s.h2b, c.h1, c.h1), 3, 1);
    auto h3b = f.hashOf(b3b);
    BOOST_REQUIRE(f.np(b3b).status == PayloadValidationStatus::Valid);
    BOOST_REQUIRE(
        f.fcu(f.fc(h3b, c.h1, c.h1)).payloadStatus.status == PayloadValidationStatus::Valid);
    auto b4b = f.build(f.fc(h3b, c.h1, c.h1), 4, 1);
    auto h4b = f.hashOf(b4b);
    BOOST_REQUIRE(f.np(b4b).status == PayloadValidationStatus::Valid);
    BOOST_REQUIRE(
        f.fcu(f.fc(h4b, c.h1, c.h1)).payloadStatus.status == PayloadValidationStatus::Valid);
    BOOST_CHECK_EQUAL(f.balanceAt(h3b), bcos::u256(16));  // B3b executed on B2b's 15, not 20
    BOOST_CHECK_EQUAL(f.balanceAt(h4b), bcos::u256(17));
    BOOST_CHECK_EQUAL(f.onChainAt(h4b, 3).value_or(bcos::h256{}).hex(), h3b.hex());
    BOOST_CHECK_EQUAL(f.onChainAt(h4b, 2).value_or(bcos::h256{}).hex(), s.h2b.hex());
    BOOST_CHECK_EQUAL(f.onChainAt(h4b, 1).value_or(bcos::h256{}).hex(), c.h1.hex());
    BOOST_CHECK(!f.onChainAt(h4b, 5).has_value());
    BOOST_CHECK(f.inWindow(c.h2a));
    BOOST_CHECK_EQUAL(f.opDelegate->windowSize(), 4U);

    // Row 3: back to B2b, a non-tip ancestor. VALID, tracker only.
    auto back = f.fcu(f.fc(s.h2b, c.h1, c.h1));
    BOOST_CHECK(back.payloadStatus.status == PayloadValidationStatus::Valid);
    BOOST_CHECK(!back.payloadId.has_value());
    BOOST_CHECK_EQUAL(f.trackedHead().value_or(bcos::h256{}).hex(), s.h2b.hex());
    BOOST_CHECK_EQUAL(f.service->getHeadBlockNumber().value_or(-1), 2);
    BOOST_CHECK_EQUAL(f.balanceAt(s.h2b), bcos::u256(15));
    BOOST_CHECK_EQUAL(f.opDelegate->windowSize(), 4U);
    BOOST_CHECK_EQUAL(f.backendNumber(), 1);

    // Row 4: finalized advances to B2b (head jumps back to B4b, safe B3b).
    auto const mintA = f.mintTxHash(c.b2a);
    auto const mintB = f.mintTxHash(s.b2b);
    f.finalizeNotifications.clear();
    auto fin = f.fcu(f.fc(h4b, h3b, s.h2b));
    BOOST_CHECK(fin.payloadStatus.status == PayloadValidationStatus::Valid);
    BOOST_CHECK_EQUAL(f.backendNumber(), 2);
    BOOST_CHECK_EQUAL(f.backendBalance(), bcos::u256(15));
    BOOST_CHECK_EQUAL(f.opDelegate->finalizedNumber(), 2);
    BOOST_CHECK_EQUAL(f.opDelegate->finalizedHash().hex(), s.h2b.hex());
    BOOST_CHECK_EQUAL(f.opDelegate->windowSize(), 2U);  // {B3b, B4b}
    BOOST_CHECK(!f.inWindow(c.h2a));
    BOOST_CHECK(!f.inWindow(s.h2b));
    BOOST_CHECK(f.inWindow(h3b));
    BOOST_CHECK(f.inWindow(h4b));
    // The side branch's transactions never reached the backend; the finalized one's did.
    BOOST_CHECK(!f.backendHasTx(mintA));
    BOOST_CHECK(f.backendHasTx(mintB));
    // The block-number notifier fires on finalize only, once per merged block.
    BOOST_REQUIRE_EQUAL(f.finalizeNotifications.size(), 1U);
    BOOST_CHECK_EQUAL(f.finalizeNotifications.front(), 2);
    // Reads through the head chain still work; B2a's view is gone.
    BOOST_CHECK_EQUAL(f.balanceAt(h4b), bcos::u256(17));
    BOOST_CHECK(!bcos::task::syncWait(f.opDelegate->viewAt(c.h2a)).has_value());
    BOOST_CHECK_EQUAL(f.onChainAt(h4b, 2).value_or(bcos::h256{}).hex(), s.h2b.hex());
    BOOST_CHECK_EQUAL(f.onChainAt(h4b, 1).value_or(bcos::h256{}).hex(), c.h1.hex());
    // Running totals across the window chain: B1(2 txs) + B2b(2) at finalized height 2.
    {
        auto view = f.multiLayerStorage.forkCommitted();
        auto total = bcos::task::syncWait(bcos::storage2::readOne(
            view, StateKey{bcos::ledger::SYS_CURRENT_STATE,
                      std::string(bcos::ledger::SYS_KEY_TOTAL_TRANSACTION_COUNT)}));
        BOOST_REQUIRE(total.has_value());
        BOOST_CHECK_EQUAL(std::string(total->get()), "4");
    }

    // §12.3 row 3 / §15 row 7: a head below finalized (B1) is -38002; a pruned sibling is
    // simply unknown (SYNCING) — the node cannot know the height of a hash it dropped.
    BOOST_CHECK_THROW(f.fcu(f.fc(c.h1, c.h1, c.h1)), bcos::engine::InvalidForkchoiceState);
    BOOST_CHECK(
        f.fcu(f.fc(c.h2a, c.h1, c.h1)).payloadStatus.status == PayloadValidationStatus::Syncing);
    BOOST_CHECK_EQUAL(f.trackedHead().value_or(bcos::h256{}).hex(), h4b.hex());
    // §12.3 row 2: a block at/below finalized with an unknown hash → SYNCING, not INVALID.
    BOOST_CHECK(f.np(c.b2a).status == PayloadValidationStatus::Syncing);
    // §12.3 row 5: finalized neither in the window nor on disk → -38002.
    BOOST_CHECK_THROW(f.fcu(f.fc(h4b, h3b, bcos::h256(std::string(64, 'f')))),
        bcos::engine::InvalidForkchoiceState);
    // The finalized tip itself is a legal head (heartbeat after a full finalize).
    BOOST_CHECK(
        f.fcu(f.fc(s.h2b, s.h2b, s.h2b)).payloadStatus.status == PayloadValidationStatus::Valid);
}

// Row 5 — op-node's backup restore: FCU(head=B2a) without attributes after B2b won.
BOOST_AUTO_TEST_CASE(row5_restore_replaced_sibling)
{
    ReorgFixture f;
    auto c = setupB1B2a(f);
    auto s = deriveB2b(f, c);

    auto restore = f.fcu(f.fc(c.h2a, c.h1, c.h1));
    BOOST_CHECK(restore.payloadStatus.status == PayloadValidationStatus::Valid);
    BOOST_CHECK(!restore.payloadId.has_value());
    BOOST_CHECK_EQUAL(f.trackedHead().value_or(bcos::h256{}).hex(), c.h2a.hex());
    BOOST_CHECK_EQUAL(f.balanceAt(c.h2a), bcos::u256(20));
    BOOST_CHECK_EQUAL(f.opDelegate->windowSize(), 2U);
    BOOST_CHECK_EQUAL(f.backendNumber(), 1);
    BOOST_CHECK_EQUAL(f.backendBalance(), bcos::u256(10));
    // And back again: switching between siblings is tracker-only, any number of times.
    BOOST_CHECK(
        f.fcu(f.fc(s.h2b, c.h1, c.h1)).payloadStatus.status == PayloadValidationStatus::Valid);
    BOOST_CHECK_EQUAL(f.trackedHead().value_or(bcos::h256{}).hex(), s.h2b.hex());
    BOOST_CHECK_EQUAL(f.balanceAt(s.h2b), bcos::u256(15));
}

// Row 6 — restart after row 4: the window is memory, the backend is the finalized chain.
BOOST_AUTO_TEST_CASE(row6_restart_refills_from_finalized)
{
    ReorgFixture f;
    auto c = setupB1B2a(f);
    auto s = deriveB2b(f, c);
    auto b3b = f.build(f.fc(s.h2b, c.h1, c.h1), 3, 1);
    auto h3b = f.hashOf(b3b);
    BOOST_REQUIRE(f.np(b3b).status == PayloadValidationStatus::Valid);
    BOOST_REQUIRE(
        f.fcu(f.fc(h3b, c.h1, c.h1)).payloadStatus.status == PayloadValidationStatus::Valid);
    auto b4b = f.build(f.fc(h3b, c.h1, c.h1), 4, 1);
    auto h4b = f.hashOf(b4b);
    BOOST_REQUIRE(f.np(b4b).status == PayloadValidationStatus::Valid);
    BOOST_REQUIRE(
        f.fcu(f.fc(h4b, h3b, s.h2b)).payloadStatus.status == PayloadValidationStatus::Valid);
    BOOST_REQUIRE_EQUAL(f.backendNumber(), 2);
    auto const balanceBefore = f.balanceAt(h4b);

    f.restart();
    // Nothing tracked, nothing in the window; finalized hydrates from disk on first use.
    BOOST_CHECK(!f.trackedHead().has_value());
    BOOST_CHECK_EQUAL(f.opDelegate->windowSize(), 0U);
    BOOST_CHECK(!f.inWindow(h3b));
    BOOST_CHECK(!f.inWindow(h4b));
    BOOST_CHECK(bcos::task::syncWait(f.opDelegate->viewAt(s.h2b)).has_value());
    BOOST_CHECK_EQUAL(f.opDelegate->finalizedNumber(), 2);
    BOOST_CHECK_EQUAL(f.opDelegate->finalizedHash().hex(), s.h2b.hex());
    BOOST_CHECK_EQUAL(f.backendBalance(), bcos::u256(15));

    // op-node's first FCU names the old head: unknown here → SYNCING (it then resets and
    // re-derives from the tags, which all answer B2b).
    BOOST_CHECK(
        f.fcu(f.fc(h4b, h3b, s.h2b)).payloadStatus.status == PayloadValidationStatus::Syncing);
    BOOST_CHECK(
        f.fcu(f.fc(s.h2b, s.h2b, s.h2b)).payloadStatus.status == PayloadValidationStatus::Valid);
    // Refill: the same payloads execute again on top of the finalized tip.
    BOOST_CHECK(f.np(b3b).status == PayloadValidationStatus::Valid);
    BOOST_CHECK(f.np(b4b).status == PayloadValidationStatus::Valid);
    BOOST_CHECK(
        f.fcu(f.fc(h4b, h3b, s.h2b)).payloadStatus.status == PayloadValidationStatus::Valid);
    BOOST_CHECK_EQUAL(f.balanceAt(h4b), balanceBefore);
    BOOST_CHECK_EQUAL(f.opDelegate->windowSize(), 2U);
    // The pruned sibling is gone for good.
    BOOST_CHECK(f.np(c.b2a).status == PayloadValidationStatus::Syncing);
    BOOST_CHECK(!f.inWindow(c.h2a));
}

// Row 8 — an externally produced side branch (§11.1, after a restart) arrives with no FCU in
// between: NP(B2a) → NP(B2b) → FCU(head=B2b). B2b is built on a twin node over the same genesis
// so that it is a genuinely external payload here (never staged by this node's build path).
// Also §12.3 row 1: a payload whose parent is unknown → SYNCING.
BOOST_AUTO_TEST_CASE(row8_external_side_branch_and_unknown_parent)
{
    ReorgFixture twin;
    auto tc = setupB1B2a(twin);
    auto external = twin.build(twin.fc(tc.h1, tc.h1, tc.h1), 2, 5);  // B2b, built elsewhere
    auto b3External = [&] {
        auto h2b = twin.hashOf(external);
        BOOST_REQUIRE(twin.np(external).status == PayloadValidationStatus::Valid);
        BOOST_REQUIRE(twin.fcu(twin.fc(h2b, tc.h1, tc.h1)).payloadStatus.status ==
                      PayloadValidationStatus::Valid);
        return twin.build(twin.fc(h2b, tc.h1, tc.h1), 3, 1);  // B3b on the twin
    }();

    ReorgFixture f;
    auto c = setupB1B2a(f);
    BOOST_REQUIRE_EQUAL(c.h1.hex(), tc.h1.hex());  // deterministic twins
    BOOST_REQUIRE_EQUAL(c.h2a.hex(), tc.h2a.hex());

    // §12.3 row 1: B3b's parent (B2b) is unknown here.
    BOOST_CHECK(f.np(b3External).status == PayloadValidationStatus::Syncing);

    // Row 8: the sibling arrives as an external payload — VALID, not SYNCING.
    auto status = f.np(external);
    BOOST_CHECK_MESSAGE(status.status == PayloadValidationStatus::Valid,
        "external sibling must be VALID"
            << (status.validationError ? ": " + *status.validationError : std::string{}));
    auto const h2b = f.hashOf(external);
    BOOST_CHECK(f.inWindow(h2b));
    BOOST_CHECK(f.inWindow(c.h2a));
    BOOST_CHECK_EQUAL(f.trackedHead().value_or(bcos::h256{}).hex(), c.h2a.hex());
    BOOST_CHECK(
        f.fcu(f.fc(h2b, c.h1, c.h1)).payloadStatus.status == PayloadValidationStatus::Valid);
    BOOST_CHECK_EQUAL(f.balanceAt(h2b), bcos::u256(15));
    // Now B3b's parent is known: it executes on B2b's chain.
    BOOST_CHECK(f.np(b3External).status == PayloadValidationStatus::Valid);
    BOOST_CHECK_EQUAL(f.balanceAt(f.hashOf(b3External)), bcos::u256(16));
    // An honest resend of an admitted payload is VALID without re-execution.
    BOOST_CHECK(f.np(external).status == PayloadValidationStatus::Valid);
    BOOST_CHECK_EQUAL(f.opDelegate->windowSize(), 3U);
}

// Window depth: newPayload above finalized + unfinalized_window answers SYNCING until the
// finalized tip advances; nothing is lost, the same payload is accepted afterwards.
BOOST_AUTO_TEST_CASE(window_depth_backpressure)
{
    ReorgFixture f(/*window=*/2);
    auto const g = f.genesisHash;
    auto b1 = f.build(f.fc(g, g, g), 1, 10);
    auto h1 = f.hashOf(b1);
    BOOST_REQUIRE(f.np(b1).status == PayloadValidationStatus::Valid);
    BOOST_REQUIRE(f.fcu(f.fc(h1, g, g)).payloadStatus.status == PayloadValidationStatus::Valid);
    auto b2 = f.build(f.fc(h1, g, g), 2, 1);
    auto h2 = f.hashOf(b2);
    BOOST_REQUIRE(f.np(b2).status == PayloadValidationStatus::Valid);
    BOOST_REQUIRE(f.fcu(f.fc(h2, g, g)).payloadStatus.status == PayloadValidationStatus::Valid);
    // Block 3 would sit 3 above the finalized tip (0) with a window of 2.
    auto b3 = f.build(f.fc(h2, g, g), 3, 1);
    auto h3 = f.hashOf(b3);
    BOOST_CHECK(f.np(b3).status == PayloadValidationStatus::Syncing);
    BOOST_CHECK(!f.inWindow(h3));
    // op-node's follow-up FCU naming the unadmitted head is SYNCING too (it retries).
    BOOST_CHECK(f.fcu(f.fc(h3, g, g)).payloadStatus.status == PayloadValidationStatus::Syncing);
    // Finalize B1: the tip moves to 1 and block 3 (depth 2) fits.
    BOOST_REQUIRE(f.fcu(f.fc(h2, h1, h1)).payloadStatus.status == PayloadValidationStatus::Valid);
    BOOST_REQUIRE_EQUAL(f.backendNumber(), 1);
    BOOST_CHECK(f.np(b3).status == PayloadValidationStatus::Valid);
    BOOST_CHECK(f.fcu(f.fc(h3, h1, h1)).payloadStatus.status == PayloadValidationStatus::Valid);
    BOOST_CHECK_EQUAL(f.balanceAt(h3), bcos::u256(12));
}

BOOST_AUTO_TEST_SUITE_END()
