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
 * @file HistoryIndexTest.cpp
 * @brief The in-memory index on its own: append, retire, the upper_bound lookup, the hand-off from
 *        a rebuild, and the state machine that decides whether it may answer at all
 *        (layout spec §1.3, G10)
 */

#include "HistoryTestHelpers.h"
#include <bcos-ledger/mpt/Errors.h>
#include <bcos-ledger/mpt/history/HistoryErrors.h>
#include <bcos-ledger/mpt/history/HistoryIndex.h>
#include <boost/test/unit_test.hpp>
#include <cstddef>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

using namespace std::string_view_literals;

namespace bcos::ledger::mpt::history::test
{

BOOST_AUTO_TEST_SUITE(HistoryIndexSuite)

namespace
{
/// One block's worth of index update, without going anywhere near a storage: the index does not
/// know what a row is, so its tests should not build any.
StagedBlock stagedFor(bcos::protocol::BlockNumber block,
    std::vector<std::pair<std::string_view, uint32_t>> const& keysAtOffsets)
{
    StagedBlock staged{.block = block,
        .meta = BlockMeta{.shardCount = 1,
            .recordCount = static_cast<uint32_t>(keysAtOffsets.size()),
            .blockHash = blockHashOf(block)},
        .versions = {}};
    for (auto const& [key, offset] : keysAtOffsets)
    {
        staged.versions.emplace_back(
            makeBytes(key), HistoryVersion{.block = block, .shard = 0, .offset = offset});
    }
    return staged;
}

std::optional<HistoryVersion> lookup(
    HistoryIndex const& index, std::string_view key, bcos::protocol::BlockNumber block)
{
    auto const keyBytes = makeBytes(key);
    return index.locate(keyBytes, block);
}
}  // namespace

/// A fresh index has never been rebuilt, and that is NOT the same as "this chain recorded no
/// history": an index that was never built cannot tell the two apart, and reading its silence as
/// "the key never changed" is exactly the wrong answer G10 forbids.
BOOST_AUTO_TEST_CASE(emptyIndexRefusesEveryQuery)
{
    HistoryIndex index;
    BOOST_CHECK(index.state() == IndexState::Empty);
    BOOST_CHECK_EQUAL(index.keyCount(), std::size_t{0});
    BOOST_CHECK_EQUAL(index.versionCount(), std::size_t{0});
    BOOST_CHECK_THROW(lookup(index, "k"sv, 5), HistoryIndexUnavailable);
    // The plain accessor is not the guarded path and answers without refusing — which is why the
    // query path uses locate() and not this.
    auto const keyBytes = makeBytes("k"sv);
    BOOST_CHECK(!index.firstChangeAfter(keyBytes, 5).has_value());
}

/// publish appends, and the versions of one key stay in ascending block order — the order the
/// upper_bound lookup and the front-of-vector retire both stand on.
BOOST_AUTO_TEST_CASE(publishAppendsInBlockOrder)
{
    HistoryIndex index;
    index.publish(stagedFor(10, {{"a"sv, 0}, {"b"sv, 12}}), std::nullopt, std::nullopt);
    index.publish(stagedFor(20, {{"a"sv, 0}}), std::nullopt, std::nullopt);
    index.publish(stagedFor(30, {{"a"sv, 4}, {"b"sv, 0}}), std::nullopt, std::nullopt);

    BOOST_CHECK(index.state() == IndexState::Ready);
    BOOST_CHECK_EQUAL(index.keyCount(), std::size_t{2});
    BOOST_CHECK_EQUAL(index.versionCount(), std::size_t{5});
    BOOST_CHECK_EQUAL(index.blockCount(), std::size_t{3});
    BOOST_CHECK(index.hasBlock(20));
    BOOST_CHECK(!index.hasBlock(21));
    auto const meta = index.blockMeta(30);
    BOOST_REQUIRE(meta.has_value());
    BOOST_CHECK_EQUAL(meta->recordCount, 2);
}

/// The lookup is `upper_bound`: the first version STRICTLY after the queried block. Block 20's own
/// record answers block 19, not block 20 — block 20's value is what block 30 recorded as its
/// pre-image. Getting this bound wrong is an off-by-one that returns a plausible neighbouring
/// value, so every boundary is spelled out.
BOOST_AUTO_TEST_CASE(firstChangeAfterIsAStrictUpperBound)
{
    HistoryIndex index;
    index.publish(stagedFor(10, {{"a"sv, 100}}), std::nullopt, std::nullopt);
    index.publish(stagedFor(20, {{"a"sv, 200}}), std::nullopt, std::nullopt);
    index.publish(stagedFor(30, {{"a"sv, 300}}), std::nullopt, std::nullopt);

    BOOST_CHECK_EQUAL(lookup(index, "a"sv, 0)->offset, 100U);
    BOOST_CHECK_EQUAL(lookup(index, "a"sv, 9)->offset, 100U);
    BOOST_CHECK_EQUAL(lookup(index, "a"sv, 10)->offset, 200U);
    BOOST_CHECK_EQUAL(lookup(index, "a"sv, 19)->offset, 200U);
    BOOST_CHECK_EQUAL(lookup(index, "a"sv, 20)->offset, 300U);
    BOOST_CHECK_EQUAL(lookup(index, "a"sv, 29)->offset, 300U);
    // Nothing recorded after block 30: the caller reads the current value.
    BOOST_CHECK(!lookup(index, "a"sv, 30).has_value());
    BOOST_CHECK(!lookup(index, "a"sv, 999).has_value());
    // A key the index has never seen is the same answer, and is only safe because the index is
    // Ready and its boundary covers the block.
    BOOST_CHECK(!lookup(index, "never"sv, 5).has_value());
    // The located version names the block AND the shard, not just the offset.
    BOOST_CHECK(
        *lookup(index, "a"sv, 10) == (HistoryVersion{.block = 20, .shard = 0, .offset = 200}));
}

/// retire drops exactly the expired block's versions, from the FRONT of each key's vector, and
/// leaves every other block's answers untouched. A key whose last version goes stops being a key.
BOOST_AUTO_TEST_CASE(retireRemovesOnlyTheExpiredBlock)
{
    HistoryIndex index;
    index.publish(stagedFor(10, {{"a"sv, 100}, {"gone"sv, 0}}), std::nullopt, std::nullopt);
    index.publish(stagedFor(20, {{"a"sv, 200}}), std::nullopt, std::nullopt);
    BOOST_REQUIRE_EQUAL(index.versionCount(), std::size_t{3});

    // The retirement rides the NEXT block's publish, which is how the commit path issues it.
    RetiredBlock retired{
        .block = 10, .keys = {makeBytes("a"sv), makeBytes("gone"sv)}, .shardsDeleted = 1};
    index.publish(stagedFor(30, {{"a"sv, 300}}), std::move(retired),
        std::optional<bcos::protocol::BlockNumber>{10});

    BOOST_CHECK(!index.hasBlock(10));
    BOOST_CHECK(index.hasBlock(20));
    BOOST_CHECK(index.hasBlock(30));
    BOOST_CHECK_EQUAL(index.versionCount(), std::size_t{2});
    // "gone" had only block 10's version, so it is no longer a key at all.
    BOOST_CHECK_EQUAL(index.keyCount(), std::size_t{1});
    // Block 10 is the boundary now, so block 9 is refused while block 10 still answers from
    // block 20's surviving record.
    BOOST_REQUIRE(index.boundary().has_value());
    BOOST_CHECK_EQUAL(*index.boundary(), 10);
    BOOST_CHECK_THROW(lookup(index, "a"sv, 9), HistoryPruned);
    BOOST_CHECK_EQUAL(lookup(index, "a"sv, 10)->offset, 200U);
}

/// The boundary only ever grows. Two callers hand expire a LOWER block — an operator raising the
/// retention depth, and a chain re-committing after a rollback — and assigning would then claim
/// heights are intact whose shards an earlier, higher expiry already deleted.
BOOST_AUTO_TEST_CASE(boundaryTakesTheMaximum)
{
    HistoryIndex index;
    index.setBoundary(std::optional<bcos::protocol::BlockNumber>{50});
    BOOST_CHECK_EQUAL(*index.boundary(), 50);
    index.setBoundary(std::optional<bcos::protocol::BlockNumber>{20});
    BOOST_CHECK_EQUAL(*index.boundary(), 50);
    index.setBoundary(std::nullopt);
    BOOST_CHECK_EQUAL(*index.boundary(), 50);
    index.setBoundary(std::optional<bcos::protocol::BlockNumber>{60});
    BOOST_CHECK_EQUAL(*index.boundary(), 60);

    // The boundary alone does not entitle the index to answer: it is still Empty.
    BOOST_CHECK(index.state() == IndexState::Empty);
    BOOST_CHECK_THROW(lookup(index, "a"sv, 70), HistoryIndexUnavailable);
}

/// replace installs a whole rebuilt index and makes it Ready — reaching replace IS the proof that
/// the rebuild walked its input end to end, because every inconsistency throws instead.
BOOST_AUTO_TEST_CASE(replaceInstallsTheRebuiltIndexAsReady)
{
    HistoryIndex live;
    live.publish(stagedFor(10, {{"stale"sv, 0}}), std::nullopt, std::nullopt);
    live.markUnavailable();
    BOOST_REQUIRE(live.state() == IndexState::Unavailable);
    BOOST_CHECK_THROW(lookup(live, "stale"sv, 5), HistoryIndexUnavailable);

    HistoryIndex rebuilt;
    rebuilt.setBoundary(std::optional<bcos::protocol::BlockNumber>{7});
    rebuilt.publish(stagedFor(11, {{"fresh"sv, 40}}), std::nullopt, std::nullopt);
    live.replace(std::move(rebuilt));

    BOOST_CHECK(live.state() == IndexState::Ready);
    // The old contents are gone, not merged.
    BOOST_CHECK(!live.hasBlock(10));
    BOOST_CHECK(live.hasBlock(11));
    BOOST_CHECK_EQUAL(live.keyCount(), std::size_t{1});
    // Above the rebuilt boundary, so this is the lookup missing rather than the boundary refusing.
    BOOST_CHECK(!lookup(live, "stale"sv, 8).has_value());
    BOOST_CHECK_EQUAL(lookup(live, "fresh"sv, 10)->offset, 40U);
    BOOST_REQUIRE(live.boundary().has_value());
    BOOST_CHECK_EQUAL(*live.boundary(), 7);

    // A rebuild that found NOTHING still installs Ready: the walk saw the whole retained range,
    // so "no recorded change" is a fact rather than an absence of knowledge.
    HistoryIndex emptyRebuild;
    live.replace(std::move(emptyRebuild));
    BOOST_CHECK(live.state() == IndexState::Ready);
    BOOST_CHECK(!lookup(live, "fresh"sv, 10).has_value());
}

/// A publish that throws leaves the maps in an unknown shape, and an index missing versions
/// answers "the key never changed". So the failure is latched: everything refuses until a rebuild
/// replaces it, and a second attempt at the same block does not quietly succeed.
BOOST_AUTO_TEST_CASE(aFailedPublishLatchesUnavailable)
{
    HistoryIndex index;
    index.publish(stagedFor(10, {{"a"sv, 0}}), std::nullopt, std::nullopt);
    BOOST_REQUIRE(index.state() == IndexState::Ready);

    // Publishing the same block twice would give key "a" two versions for block 10, and retiring
    // block 10 would then leave one behind.
    BOOST_CHECK_THROW(index.publish(stagedFor(10, {{"a"sv, 8}}), std::nullopt, std::nullopt),
        MPTInvariantViolation);
    BOOST_CHECK(index.state() == IndexState::Unavailable);
    BOOST_CHECK_THROW(lookup(index, "a"sv, 5), HistoryIndexUnavailable);
    // The duplicate did not land: the check runs before any version vector is touched.
    BOOST_CHECK_EQUAL(index.versionCount(), std::size_t{1});
}

/// The ordering precondition the whole lookup rests on. Versions are appended with push_back, so a
/// key's vector is sorted only because the block numbers arrived ascending; publish a block BELOW
/// the current maximum and `upper_bound` is reading an unsorted range, which is undefined and comes
/// back as a plausible wrong version rather than as an error.
///
/// So a descending publish is refused outright, and refused the same way a duplicate is — the two
/// are one check, because a duplicate is the equality case of "not strictly ascending".
BOOST_AUTO_TEST_CASE(publishingBlocksOutOfOrderIsRefused)
{
    HistoryIndex index;
    index.publish(stagedFor(20, {{"a"sv, 200}}), std::nullopt, std::nullopt);
    BOOST_REQUIRE(index.state() == IndexState::Ready);

    BOOST_CHECK_THROW(index.publish(stagedFor(10, {{"a"sv, 100}}), std::nullopt, std::nullopt),
        MPTInvariantViolation);
    BOOST_CHECK(index.state() == IndexState::Unavailable);
    BOOST_CHECK_THROW(lookup(index, "a"sv, 5), HistoryIndexUnavailable);
    // Nothing landed: block 10 is absent and key "a" still has exactly block 20's version, so the
    // vector the refused publish would have unsorted is still sorted.
    BOOST_CHECK(!index.hasBlock(10));
    BOOST_CHECK(index.hasBlock(20));
    BOOST_CHECK_EQUAL(index.versionCount(), std::size_t{1});

    // Ascending still works, on an index that has not been poisoned.
    HistoryIndex fresh;
    fresh.publish(stagedFor(10, {{"a"sv, 100}}), std::nullopt, std::nullopt);
    fresh.publish(stagedFor(20, {{"a"sv, 200}}), std::nullopt, std::nullopt);
    BOOST_CHECK_EQUAL(lookup(fresh, "a"sv, 9)->offset, 100U);
    BOOST_CHECK_EQUAL(lookup(fresh, "a"sv, 10)->offset, 200U);
}

/// The publish generation: the counter a reader uses to tell "the index is quiescent" from "a
/// commit has already put its rows on disk and has not told me yet".
///
/// Everything about it is a parity claim, so the case is about parity: even at rest, odd while a
/// window is open, and even again once the publish that closes the window has run. The value must
/// also MOVE across a publish — a reader compares two samples across its own read of the current
/// value, and a counter that only toggled parity without advancing would let an open-then-closed
/// pair look like no publish at all.
BOOST_AUTO_TEST_CASE(thePublishGenerationTracksTheWindow)
{
    HistoryIndex index;
    BOOST_CHECK_EQUAL(index.generation() % 2, 0U);
    auto const atRest = index.generation();

    // Opening is idempotent: the commit path opens once per block, but a second open must not
    // toggle the parity back to "quiescent" while the window is still held.
    index.openPublishWindow();
    BOOST_CHECK_EQUAL(index.generation() % 2, 1U);
    auto const opened = index.generation();
    index.openPublishWindow();
    BOOST_CHECK_EQUAL(index.generation(), opened);
    BOOST_CHECK_GT(opened, atRest);

    // publish closes the window it finds open...
    index.publish(stagedFor(10, {{"a"sv, 100}}), std::nullopt, std::nullopt);
    BOOST_CHECK_EQUAL(index.generation() % 2, 0U);
    auto const afterPublish = index.generation();
    BOOST_CHECK_GT(afterPublish, opened);

    // ...and closing is idempotent too, so the RAII guard's destructor after a successful publish
    // does not open a phantom window by flipping the parity again.
    index.closePublishWindow();
    BOOST_CHECK_EQUAL(index.generation(), afterPublish);

    // A publish with no window open still advances the counter, because a reader's two samples
    // straddling it must differ.
    index.publish(stagedFor(20, {{"a"sv, 200}}), std::nullopt, std::nullopt);
    BOOST_CHECK_EQUAL(index.generation() % 2, 0U);
    BOOST_CHECK_GT(index.generation(), afterPublish);

    // A publish that THROWS must not leave the window open — the commit died, and a permanently
    // open window would make every later "unchanged since B" query spin and then refuse forever
    // rather than only until the node notices.
    index.openPublishWindow();
    BOOST_CHECK_EQUAL(index.generation() % 2, 1U);
    BOOST_CHECK_THROW(index.publish(stagedFor(15, {{"a"sv, 150}}), std::nullopt, std::nullopt),
        MPTInvariantViolation);
    BOOST_CHECK(index.state() == IndexState::Unavailable);
    BOOST_CHECK_EQUAL(index.generation() % 2, 0U);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::history::test
