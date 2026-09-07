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
 * @brief The point query: the spec §0.4 timeline, the UseCurrent case, and the window guard that
 *        has to run before the seek (spec B.3, G5)
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
/// The spec §0.4 timeline: X's balance is changed by blocks 100, 107, 140 and 900. Each row
/// records what the balance was BEFORE that block changed it.
///
/// ```text
///   block   100        107        140                900
///   before  "60"       "90"       "75"               "42"
/// ```
void seedSection04Timeline(HistoryMemStorage& storage)
{
    Diff at100;
    at100.change("X.balance"sv, "60"sv);
    Diff at107;
    at107.change("X.balance"sv, "90"sv);
    Diff at140;
    at140.change("X.balance"sv, "75"sv);
    Diff at900;
    at900.change("X.balance"sv, "42"sv);
    putBlock(storage, 100, at100);
    putBlock(storage, 107, at107);
    putBlock(storage, 140, at140);
    putBlock(storage, 900, at900);
}
}  // namespace

/// spec §0.4, worked through: "the old value recorded by the first change AFTER B is exactly the
/// value at B", because nothing touched the key in between.
BOOST_AUTO_TEST_CASE(section04Timeline)
{
    HistoryMemStorage storage;
    seedSection04Timeline(storage);
    constexpr bcos::protocol::BlockNumber tip = 1000;
    constexpr bcos::protocol::BlockNumber depth = 1000;

    // Asked for block 107, the first change after it is block 140, whose row says "before = 75".
    auto at107 = readAt(storage, "X.balance"sv, 107, tip, depth);
    BOOST_REQUIRE(std::holds_alternative<bcos::bytes>(at107));
    BOOST_CHECK_EQUAL(toText(std::get<bcos::bytes>(at107)), "75");

    // Every block from 107 to 139 has the same answer — that is the property the layout buys.
    for (bcos::protocol::BlockNumber block : {107, 108, 120, 139})
    {
        auto result = readAt(storage, "X.balance"sv, block, tip, depth);
        BOOST_REQUIRE(std::holds_alternative<bcos::bytes>(result));
        BOOST_CHECK_EQUAL(toText(std::get<bcos::bytes>(result)), "75");
    }

    // Block 100 itself: the next change is 107, whose row says "before = 90".
    auto at100 = readAt(storage, "X.balance"sv, 100, tip, depth);
    BOOST_REQUIRE(std::holds_alternative<bcos::bytes>(at100));
    BOOST_CHECK_EQUAL(toText(std::get<bcos::bytes>(at100)), "90");

    // Just before the first recorded change, the 100 row applies.
    auto at99 = readAt(storage, "X.balance"sv, 99, tip, depth);
    BOOST_REQUIRE(std::holds_alternative<bcos::bytes>(at99));
    BOOST_CHECK_EQUAL(toText(std::get<bcos::bytes>(at99)), "60");

    // Block 899: the next change is 900.
    auto at899 = readAt(storage, "X.balance"sv, 899, tip, depth);
    BOOST_REQUIRE(std::holds_alternative<bcos::bytes>(at899));
    BOOST_CHECK_EQUAL(toText(std::get<bcos::bytes>(at899)), "42");
}

/// spec §0.4: "block 950 — nothing changed after 900, so the current flat value is the answer."
/// The store says so rather than guessing a value it does not hold.
BOOST_AUTO_TEST_CASE(noChangeAfterBlockReturnsUseCurrent)
{
    HistoryMemStorage storage;
    seedSection04Timeline(storage);

    // Compared as a whole variant, not just probed for its alternative: PR-C's callers switch on
    // ReadAtResult, so it has to be an equality-comparable value.
    BOOST_CHECK(
        readAt(storage, "X.balance"sv, 950, 1000, 1000) == ReadAtResult{HistoryUseCurrent{}});
    // The tip itself is always UseCurrent: no block after it can have recorded a pre-image.
    BOOST_CHECK(std::holds_alternative<HistoryUseCurrent>(
        readAt(storage, "X.balance"sv, 1000, 1000, 1000)));
    // A key with no history at all is likewise UseCurrent.
    BOOST_CHECK(std::holds_alternative<HistoryUseCurrent>(
        readAt(storage, "never-touched"sv, 500, 1000, 1000)));
}

/// G5 / spec B.3, the one place this design could return a wrong answer with no error and no
/// warning. Block 872 has fallen out of a 128-block window at tip 1000, and its index rows are
/// gone — but "gone" and "never changed" look identical from a seek. The guard is what separates
/// them, and it has to run first.
///
/// This case is also the positive half of a negative control: build the same binary with
/// `-DHISTORY_GUARD_DISABLED` (see ReverseHistoryStore::readAt) and it fails, because without
/// the guard the seek runs, lands on the block-900 row, and hands back "42" — a value that is
/// entirely plausible and entirely wrong for block 872, which predates the retained window.
BOOST_AUTO_TEST_CASE(outOfWindowQueryThrowsHistoryPruned)
{
    HistoryMemStorage storage;
    seedSection04Timeline(storage);
    constexpr bcos::protocol::BlockNumber tip = 1000;
    constexpr bcos::protocol::BlockNumber depth = 128;  // window = blocks 873..1000

    BOOST_CHECK_THROW(readAt(storage, "X.balance"sv, 872, tip, depth), HistoryPruned);
    // One block inside the boundary still answers, so the case above is testing the boundary and
    // not simply a broken query.
    // "42" is also exactly what the unguarded build hands back for block 872: the seek finds the
    // block-900 row either way. Plausible, wrong, and unmarked — hence the guard.
    auto at873 = readAt(storage, "X.balance"sv, 873, tip, depth);
    BOOST_REQUIRE(std::holds_alternative<bcos::bytes>(at873));
    BOOST_CHECK_EQUAL(toText(std::get<bcos::bytes>(at873)), "42");
}

/// The second guard line: a block the chain has not reached is not a history question at all.
BOOST_AUTO_TEST_CASE(blockAheadOfTipThrowsInvalidHistoryBlock)
{
    HistoryMemStorage storage;
    seedSection04Timeline(storage);

    BOOST_CHECK_THROW(readAt(storage, "X.balance"sv, 1001, 1000, 1000), InvalidHistoryBlock);
    BOOST_CHECK_THROW(readAt(storage, "X.balance"sv, -1, 1000, 1000), InvalidHistoryBlock);
}

/// The window is [tip - depth + 1, tip], straight from spec B.3. Depth 0 makes it empty, so even
/// the tip is refused; depth 1 is the smallest window that admits anything, and it admits exactly
/// the tip. Spelling this out keeps the off-by-one honest: nothing here is a special case.
BOOST_AUTO_TEST_CASE(windowBoundsFollowTipMinusDepthPlusOne)
{
    HistoryMemStorage storage;
    seedSection04Timeline(storage);

    BOOST_CHECK_THROW(readAt(storage, "X.balance"sv, 1000, 1000, 0), HistoryPruned);
    BOOST_CHECK_THROW(readAt(storage, "X.balance"sv, 999, 1000, 0), HistoryPruned);

    BOOST_CHECK(
        std::holds_alternative<HistoryUseCurrent>(readAt(storage, "X.balance"sv, 1000, 1000, 1)));
    BOOST_CHECK_THROW(readAt(storage, "X.balance"sv, 999, 1000, 1), HistoryPruned);
}

/// A chain shorter than the window: tip - depth + 1 goes negative and must admit every block
/// down to genesis rather than wrapping and rejecting them all.
BOOST_AUTO_TEST_CASE(windowWiderThanTheChainAdmitsGenesis)
{
    HistoryMemStorage storage;
    Diff at3;
    at3.change("k"sv, "v0"sv);
    putBlock(storage, 3, at3);

    auto atZero = readAt(storage, "k"sv, 0, 5, 128);
    BOOST_REQUIRE(std::holds_alternative<bcos::bytes>(atZero));
    BOOST_CHECK_EQUAL(toText(std::get<bcos::bytes>(atZero)), "v0");
}

/// G6: an index row that is present but logically deleted means the pre-image chain has a hole
/// inside a window that claims to cover it. Falling through to the current value here would be
/// the same silent wrong answer the window guard exists to prevent, so it throws.
BOOST_AUTO_TEST_CASE(deletedIndexRowInsideTheWindowFailsLoud)
{
    HistoryLogicalDeleteStorage storage;
    Diff at50;
    at50.change("k"sv, "old"sv);
    putBlock(storage, 50, at50);

    auto keyBytes = makeBytes("k"sv);
    auto before = bcos::task::syncWait(StateHistoryStore::readAt(storage, keyBytes, 40, 100, 100));
    BOOST_REQUIRE(std::holds_alternative<bcos::bytes>(before));

    bcos::task::syncWait(bcos::storage2::removeOne(
        storage, bcos::executor_v1::StateKey{kStateHistory.index, indexRowKey(keyBytes, 50)}));

    BOOST_CHECK_THROW(
        bcos::task::syncWait(StateHistoryStore::readAt(storage, keyBytes, 40, 100, 100)),
        MPTInvariantViolation);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::history::test
