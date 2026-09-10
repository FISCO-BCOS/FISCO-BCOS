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
 * @file HistoryQueryTest.cpp
 * @brief The point query: the spec §0.4 timeline, the UseCurrent case, the window guard that has to
 *        run before everything else, and the three ways a query must refuse rather than answer
 *        from the current value (spec B.3, layout spec §1.4, G5, G6, G10)
 */

#include "HistoryTestHelpers.h"
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/mpt/Errors.h>
#include <bcos-ledger/mpt/history/HistoryErrors.h>
#include <bcos-ledger/mpt/history/HistoryRowCodec.h>
#include <bcos-ledger/mpt/history/HistoryTables.h>
#include <bcos-ledger/mpt/history/ReverseHistoryStore.h>
#include <bcos-task/Wait.h>
#include <boost/test/unit_test.hpp>
#include <string>
#include <string_view>
#include <variant>

using namespace std::string_view_literals;

namespace bcos::ledger::mpt::history::test
{

BOOST_AUTO_TEST_SUITE(HistoryQuerySuite)

namespace
{
/// The spec §0.4 timeline: X's balance is changed by blocks 100, 107, 140 and 900. Each record
/// holds what the balance was BEFORE that block changed it.
///
/// ```text
///   block   100        107        140                900
///   before  "60"       "90"       "75"               "42"
/// ```
void seedQueryTimeline(StateHistoryStore& store, HistoryMemStorage& storage)
{
    Diff at100;
    at100.change("X.balance"sv, "60"sv);
    Diff at107;
    at107.change("X.balance"sv, "90"sv);
    Diff at140;
    at140.change("X.balance"sv, "75"sv);
    Diff at900;
    at900.change("X.balance"sv, "42"sv);
    putBlock(store, storage, 100, at100);
    putBlock(store, storage, 107, at107);
    putBlock(store, storage, 140, at140);
    putBlock(store, storage, 900, at900);
}
}  // namespace

/// spec §0.4, worked through: "the old value recorded by the first change AFTER B is exactly the
/// value at B", because nothing touched the key in between.
BOOST_AUTO_TEST_CASE(section04Timeline)
{
    HistoryMemStorage storage;
    StateHistoryStore store;
    seedQueryTimeline(store, storage);
    constexpr bcos::protocol::BlockNumber tip = 1000;
    constexpr bcos::protocol::BlockNumber depth = 1000;

    // Asked for block 107, the first change after it is block 140, whose record says "before = 75".
    auto const at107 = readAt(store, storage, "X.balance"sv, 107, tip, depth);
    BOOST_REQUIRE(std::holds_alternative<bcos::bytes>(at107));
    BOOST_CHECK_EQUAL(toText(std::get<bcos::bytes>(at107)), "75");

    // Every block from 107 to 139 has the same answer — that is the property the layout buys.
    for (bcos::protocol::BlockNumber block : {107, 108, 120, 139})
    {
        auto const result = readAt(store, storage, "X.balance"sv, block, tip, depth);
        BOOST_REQUIRE(std::holds_alternative<bcos::bytes>(result));
        BOOST_CHECK_EQUAL(toText(std::get<bcos::bytes>(result)), "75");
    }

    // Block 100 itself: the next change is 107, whose record says "before = 90".
    auto const at100 = readAt(store, storage, "X.balance"sv, 100, tip, depth);
    BOOST_REQUIRE(std::holds_alternative<bcos::bytes>(at100));
    BOOST_CHECK_EQUAL(toText(std::get<bcos::bytes>(at100)), "90");

    // Just before the first recorded change, the block-100 record applies.
    auto const at99 = readAt(store, storage, "X.balance"sv, 99, tip, depth);
    BOOST_REQUIRE(std::holds_alternative<bcos::bytes>(at99));
    BOOST_CHECK_EQUAL(toText(std::get<bcos::bytes>(at99)), "60");

    // Block 899: the next change is 900.
    auto const at899 = readAt(store, storage, "X.balance"sv, 899, tip, depth);
    BOOST_REQUIRE(std::holds_alternative<bcos::bytes>(at899));
    BOOST_CHECK_EQUAL(toText(std::get<bcos::bytes>(at899)), "42");
}

/// spec §0.4: "block 950 — nothing changed after 900, so the current flat value is the answer."
/// The store says so rather than guessing a value it does not hold.
BOOST_AUTO_TEST_CASE(noChangeAfterBlockReturnsUseCurrent)
{
    HistoryMemStorage storage;
    StateHistoryStore store;
    seedQueryTimeline(store, storage);

    // Compared as a whole variant, not just probed for its alternative: PR-C's callers switch on
    // ReadAtResult, so it has to be an equality-comparable value.
    BOOST_CHECK(readAt(store, storage, "X.balance"sv, 950, 1000, 1000) ==
                ReadAtResult{HistoryUseCurrent{}});
    // The tip itself is always UseCurrent: no block after it can have recorded a pre-image, so the
    // query short-circuits instead of looking for a change above the tip.
    BOOST_CHECK(std::holds_alternative<HistoryUseCurrent>(
        readAt(store, storage, "X.balance"sv, 1000, 1000, 1000)));
    // A key with no history at all is likewise UseCurrent.
    BOOST_CHECK(std::holds_alternative<HistoryUseCurrent>(
        readAt(store, storage, "never-touched"sv, 500, 1000, 1000)));
}

/// tag 0x00 and tag 0x01 stay two distinguishable answers all the way out of readAt: an account
/// created in a block must not read back as an account that held the empty string.
BOOST_AUTO_TEST_CASE(absentAndEmptyValueAreDifferentAnswers)
{
    HistoryMemStorage storage;
    StateHistoryStore store;
    Diff diff;
    diff.change("created"sv, std::nullopt).change("emptied"sv, ""sv);
    putBlock(store, storage, 7, diff);

    BOOST_CHECK(
        std::holds_alternative<HistoryAbsent>(readAt(store, storage, "created"sv, 6, 50, 50)));
    auto const emptied = readAt(store, storage, "emptied"sv, 6, 50, 50);
    BOOST_REQUIRE(std::holds_alternative<bcos::bytes>(emptied));
    BOOST_CHECK(std::get<bcos::bytes>(emptied).empty());
}

/// G5 / spec B.3, the one place this design could return a wrong answer with no error and no
/// warning. Block 872 has fallen out of a 128-block window at tip 1000 — but "expired" and "never
/// changed" look identical to a lookup. The guard is what separates them, and it has to run first.
///
/// This case is also the positive half of a negative control: build the same binary with
/// `-DHISTORY_GUARD_DISABLED` (see ReverseHistoryStore::readAt) and it fails, because without the
/// guard the lookup runs, finds the block-900 version, and hands back "42" — a value that is
/// entirely plausible and entirely wrong for block 872, which predates the retained window.
BOOST_AUTO_TEST_CASE(outOfWindowQueryThrowsHistoryPruned)
{
    HistoryMemStorage storage;
    StateHistoryStore store;
    seedQueryTimeline(store, storage);
    constexpr bcos::protocol::BlockNumber tip = 1000;
    constexpr bcos::protocol::BlockNumber depth = 128;  // window = blocks 873..1000

    BOOST_CHECK_THROW(readAt(store, storage, "X.balance"sv, 872, tip, depth), HistoryPruned);
    // One block inside the boundary still answers, so the case above is testing the boundary and
    // not simply a broken query. "42" is also exactly what the unguarded build hands back for
    // block 872: the lookup finds the block-900 version either way. Plausible, wrong, and
    // unmarked — hence the guard.
    auto const at873 = readAt(store, storage, "X.balance"sv, 873, tip, depth);
    BOOST_REQUIRE(std::holds_alternative<bcos::bytes>(at873));
    BOOST_CHECK_EQUAL(toText(std::get<bcos::bytes>(at873)), "42");
}

/// The second guard line: a block the chain has not reached is not a history question at all.
BOOST_AUTO_TEST_CASE(blockAheadOfTipThrowsInvalidHistoryBlock)
{
    HistoryMemStorage storage;
    StateHistoryStore store;
    seedQueryTimeline(store, storage);

    BOOST_CHECK_THROW(readAt(store, storage, "X.balance"sv, 1001, 1000, 1000), InvalidHistoryBlock);
    BOOST_CHECK_THROW(readAt(store, storage, "X.balance"sv, -1, 1000, 1000), InvalidHistoryBlock);
}

/// The window is [tip - depth + 1, tip], straight from spec B.3. Depth 0 makes it empty, so even
/// the tip is refused; depth 1 is the smallest window that admits anything, and it admits exactly
/// the tip. Spelling this out keeps the off-by-one honest — including that the block == tip
/// short-circuit does NOT run ahead of the guard.
BOOST_AUTO_TEST_CASE(windowBoundsFollowTipMinusDepthPlusOne)
{
    HistoryMemStorage storage;
    StateHistoryStore store;
    seedQueryTimeline(store, storage);

    BOOST_CHECK_THROW(readAt(store, storage, "X.balance"sv, 1000, 1000, 0), HistoryPruned);
    BOOST_CHECK_THROW(readAt(store, storage, "X.balance"sv, 999, 1000, 0), HistoryPruned);

    BOOST_CHECK(std::holds_alternative<HistoryUseCurrent>(
        readAt(store, storage, "X.balance"sv, 1000, 1000, 1)));
    BOOST_CHECK_THROW(readAt(store, storage, "X.balance"sv, 999, 1000, 1), HistoryPruned);
}

/// A chain shorter than the window: tip - depth + 1 goes negative and must admit every block down
/// to genesis rather than wrapping and rejecting them all.
BOOST_AUTO_TEST_CASE(windowWiderThanTheChainAdmitsGenesis)
{
    HistoryMemStorage storage;
    StateHistoryStore store;
    Diff at3;
    at3.change("k"sv, "v0"sv);
    putBlock(store, storage, 3, at3);

    auto const atZero = readAt(store, storage, "k"sv, 0, 5, 128);
    BOOST_REQUIRE(std::holds_alternative<bcos::bytes>(atZero));
    BOOST_CHECK_EQUAL(toText(std::get<bcos::bytes>(atZero)), "v0");
}

/// G10, refusal one: the index has never been rebuilt. The rows are all on disk and a lookup would
/// find nothing, which reads as "the key never changed" — today's value under an old block's
/// number. A store that has not been rebuilt therefore refuses outright.
BOOST_AUTO_TEST_CASE(unrebuiltStoreThrowsHistoryIndexUnavailable)
{
    HistoryMemStorage storage;
    StateHistoryStore writer;
    Diff at100;
    at100.change("k"sv, "old"sv);
    putBlock(writer, storage, 100, at100);

    // A second store over the same rows: same disk, empty index.
    StateHistoryStore fresh;
    BOOST_CHECK(fresh.index().state() == IndexState::Empty);
    BOOST_CHECK_THROW(readAt(fresh, storage, "k"sv, 99, 200, 200), HistoryIndexUnavailable);
    // ...and it does not answer UseCurrent for an untouched key either — the refusal is about the
    // index, not about this key.
    BOOST_CHECK_THROW(readAt(fresh, storage, "never"sv, 99, 200, 200), HistoryIndexUnavailable);

    // Once rebuilt it answers, which is what makes the refusal above a state check and not a
    // permanently broken store.
    bcos::task::syncWait(fresh.rebuild(storage));
    auto const answered = readAt(fresh, storage, "k"sv, 99, 200, 200);
    BOOST_REQUIRE(std::holds_alternative<bcos::bytes>(answered));
    BOOST_CHECK_EQUAL(toText(std::get<bcos::bytes>(answered)), "old");

    // ...and a store the startup path marked unusable goes back to refusing.
    fresh.markUnavailable();
    BOOST_CHECK_THROW(readAt(fresh, storage, "k"sv, 99, 200, 200), HistoryIndexUnavailable);
}

/// G10, refusal two: the index located a version and the shard it named is gone. That is what an
/// expiry landing between the lookup and the read looks like — and on the mutable layer it is also
/// what a deletion sentinel looks like, since readOne reports both as absent. Falling through to
/// the current value here is the silent wrong answer the layout exists to prevent.
BOOST_AUTO_TEST_CASE(locatedButDeletedShardThrowsHistoryPruned)
{
    HistoryMemStorage storage;
    StateHistoryStore store;
    Diff at100;
    at100.change("k"sv, "value-at-99"sv);
    putBlock(store, storage, 100, at100);

    // Undisturbed, block 99 answers from block 100's record.
    auto const before = readAt(store, storage, "k"sv, 99, 200, 200);
    BOOST_REQUIRE(std::holds_alternative<bcos::bytes>(before));

    // Now delete the shard behind the index's back — the disk moved, the index did not.
    deleteRow(storage, kStateHistory.shard, shardRowKey(100, 0));
    BOOST_CHECK_THROW(readAt(store, storage, "k"sv, 99, 200, 200), HistoryPruned);
    // Emphatically NOT UseCurrent, which is the answer a fall-through would have produced.
    BOOST_CHECK(store.index().state() == IndexState::Ready);

    // Same story on a logical-deletion layer, where the row is still there as a sentinel.
    HistoryLogicalDeleteStorage mutableLayer;
    StateHistoryStore layerStore;
    Diff at50;
    at50.change("k"sv, "old"sv);
    putBlock(layerStore, mutableLayer, 50, at50);
    BOOST_REQUIRE(
        std::holds_alternative<bcos::bytes>(readAt(layerStore, mutableLayer, "k"sv, 40, 100, 100)));
    deleteRow(mutableLayer, kStateHistory.shard, shardRowKey(50, 0));
    BOOST_CHECK_THROW(readAt(layerStore, mutableLayer, "k"sv, 40, 100, 100), HistoryPruned);
}

/// G10, refusal three: the index's offset no longer names this key's record. The index and the
/// shard disagree about the layout of the same bytes, and the record sitting at that offset belongs
/// to some other key — returning it would answer with another key's value.
///
/// The corruption is built to be undetectable by length alone: the two records are the same size,
/// so every offset the index holds still points at a valid record, just the wrong one.
BOOST_AUTO_TEST_CASE(offsetPointingAtAnotherKeyFailsLoud)
{
    HistoryMemStorage storage;
    StateHistoryStore store;
    Diff diff;
    diff.change("aaa"sv, "111"sv).change("bbb"sv, "222"sv);
    putBlock(store, storage, 10, diff);

    // Sanity: each key resolves to its own value first.
    BOOST_CHECK_EQUAL(
        toText(std::get<bcos::bytes>(readAt(store, storage, "aaa"sv, 9, 20, 20))), "111");
    BOOST_CHECK_EQUAL(
        toText(std::get<bcos::bytes>(readAt(store, storage, "bbb"sv, 9, 20, 20))), "222");

    // Rewrite the shard with the two records swapped: same bytes, same length, opposite order.
    auto const keyA = makeBytes("aaa"sv);
    auto const keyB = makeBytes("bbb"sv);
    auto const valueA = makeBytes("111"sv);
    auto const valueB = makeBytes("222"sv);
    std::string swapped;
    appendRecord(swapped, keyB, std::span<const bcos::byte>(valueB));
    appendRecord(swapped, keyA, std::span<const bcos::byte>(valueA));
    auto const original = rowsOfTable(storage, kStateHistory.shard);
    BOOST_REQUIRE_EQUAL(original[1].second.size(), swapped.size());
    overwriteRow(storage, kStateHistory.shard, shardRowKey(10, 0), swapped);

    BOOST_CHECK_THROW(readAt(store, storage, "aaa"sv, 9, 20, 20), MPTInvariantViolation);
    BOOST_CHECK_THROW(readAt(store, storage, "bbb"sv, 9, 20, 20), MPTInvariantViolation);
}

/// The boundary beats the retention parameter when they disagree: a depth wide enough to admit the
/// block does not make records exist. The boundary is what the store actually did; the depth is
/// only the configured intent.
BOOST_AUTO_TEST_CASE(blockBelowTheBoundaryIsPrunedEvenInsideTheParameterWindow)
{
    HistoryMemStorage storage;
    StateHistoryStore store;
    Diff at100;
    at100.change("k"sv, "old"sv);
    Diff at120;
    at120.change("k"sv, "newer"sv);
    putBlock(store, storage, 100, at100);
    putBlock(store, storage, 120, at120);

    // The commit path's shape: expire block 100 and publish the retirement with the next block.
    auto const report = expireBlockAdvancing(store, storage, 100);
    BOOST_REQUIRE(report.retired.has_value());
    Diff at130;
    at130.change("other"sv, "x"sv);
    store.publish(stageBlock(store, storage, 130, at130), report.retired,
        std::optional<bcos::protocol::BlockNumber>{100});

    // depth 1000 admits block 99 outright, but the boundary says everything below 100 is gone.
    BOOST_CHECK_THROW(readAt(store, storage, "k"sv, 99, 200, 1000), HistoryPruned);
    // Block 100 itself is still answerable — expiring block 100 removes what answered 99.
    auto const at100Result = readAt(store, storage, "k"sv, 100, 200, 1000);
    BOOST_REQUIRE(std::holds_alternative<bcos::bytes>(at100Result));
    BOOST_CHECK_EQUAL(toText(std::get<bcos::bytes>(at100Result)), "newer");
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::history::test
