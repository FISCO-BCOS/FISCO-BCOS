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
 * @file HistoryRebuildTest.cpp
 * @brief The index is derived data, so a restart has to recompute it from the shards and get the
 *        SAME answers — and refuse outright when the shards do not add up (layout spec §1.5, G9)
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
#include <cstddef>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using namespace std::string_view_literals;

namespace bcos::ledger::mpt::history::test
{

BOOST_AUTO_TEST_SUITE(HistoryRebuildSuite)

namespace
{
constexpr bcos::protocol::BlockNumber kRebuildFirstBlock = 10;
constexpr bcos::protocol::BlockNumber kRebuildBlockCount = 6;
/// 6-byte keys with 8-byte values make a 23-byte record; a 40-byte cap puts one record per shard
/// for most blocks, so the walk has to match several shard ordinals per block rather than one.
constexpr std::size_t kRebuildShardCap = 40;

std::string rebuildKey(int index)
{
    return "key-" + std::to_string(index) + "!";  // 6 bytes
}

/// Six blocks, each changing a rotating subset of five keys, so the version vectors have different
/// lengths and the upper_bound lookups are not all trivially the same.
void seedRebuildChain(auto& store, auto& storage)
{
    for (bcos::protocol::BlockNumber block = kRebuildFirstBlock;
        block < kRebuildFirstBlock + kRebuildBlockCount; ++block)
    {
        Diff diff;
        for (int index = 0; index < 5; ++index)
        {
            if ((block + index) % 2 == 0)
            {
                diff.change(rebuildKey(index), "old-" + std::to_string(block) + "!!");
            }
        }
        putBlock(store, storage, block, diff, kRebuildShardCap);
    }
}

/// Every (key, block) answer the two stores can be asked for, compared pair by pair.
void checkSameAnswers(auto& expected, auto& actual, auto& storage)
{
    constexpr bcos::protocol::BlockNumber kTip = 100;
    for (int index = 0; index < 6; ++index)
    {
        for (bcos::protocol::BlockNumber block = kRebuildFirstBlock - 1;
            block <= kRebuildFirstBlock + kRebuildBlockCount; ++block)
        {
            auto const key = rebuildKey(index);
            BOOST_REQUIRE_MESSAGE(readAt(expected, storage, key, block, kTip, kTip) ==
                                      readAt(actual, storage, key, block, kTip, kTip),
                "rebuilt index disagrees for " << key << " at block " << block);
        }
    }
}
}  // namespace

/// A restart: the rows are on disk, the index is not. Walking the shards has to reproduce every
/// answer the live index gave, because the index is nothing but a second reading of those rows.
BOOST_AUTO_TEST_CASE(rebuildReproducesEveryAnswer)
{
    HistoryMemStorage storage;
    StateHistoryStore live;
    seedRebuildChain(live, storage);

    StateHistoryStore restarted;
    auto const report = bcos::task::syncWait(restarted.rebuild(storage));

    BOOST_CHECK_EQUAL(report.blocks, std::size_t{kRebuildBlockCount});
    BOOST_CHECK_GT(report.bytesScanned, std::size_t{0});
    // Every version came from a record, and every record was counted by a meta row.
    std::size_t declaredRecords = 0;
    for (auto const& [rowKey, rowValue] : rowsOfTable(storage, kStateHistory.shard))
    {
        if (rowKey.size() == kMetaRowKeyBytes)
        {
            declaredRecords += decodeMeta(rowValue).recordCount;
        }
    }
    BOOST_CHECK_EQUAL(report.records, declaredRecords);
    BOOST_CHECK_EQUAL(restarted.index().versionCount(), declaredRecords);
    BOOST_CHECK_EQUAL(restarted.index().versionCount(), live.index().versionCount());
    BOOST_CHECK_EQUAL(restarted.index().keyCount(), live.index().keyCount());
    BOOST_CHECK_EQUAL(restarted.index().blockCount(), live.index().blockCount());
    BOOST_CHECK(restarted.index().state() == IndexState::Ready);

    checkSameAnswers(live, restarted, storage);
}

/// A rebuild starts at `boundary + 1`, not at block 0. Rows below the boundary are residue an
/// interrupted expiry left behind, and indexing them would resurrect heights the store has already
/// promised not to answer for — the boundary would say "pruned" while the index said "here it is".
BOOST_AUTO_TEST_CASE(boundaryMakesRebuildSkipTheRowsBelowIt)
{
    HistoryMemStorage storage;
    StateHistoryStore live;
    seedRebuildChain(live, storage);
    // Blocks 10..12 were expired, but their rows were left behind by an interrupted pass.
    bcos::task::syncWait(StateHistoryStore::writeRetentionBoundary(storage, 12));

    StateHistoryStore restarted;
    auto const report = bcos::task::syncWait(restarted.rebuild(storage));

    // Only blocks 13, 14 and 15 are indexed; the residue below the boundary is not.
    BOOST_CHECK_EQUAL(report.blocks, std::size_t{3});
    BOOST_CHECK(!restarted.recordedBlock(12));
    BOOST_CHECK(restarted.recordedBlock(13));
    BOOST_REQUIRE(restarted.index().boundary().has_value());
    BOOST_CHECK_EQUAL(*restarted.index().boundary(), 12);
    // ...and the boundary it read is enforced, so a query below it refuses rather than answering
    // from the residue.
    BOOST_CHECK_THROW(readAt(restarted, storage, rebuildKey(0), 11, 100, 100), HistoryPruned);
    BOOST_CHECK_NO_THROW(readAt(restarted, storage, rebuildKey(0), 13, 100, 100));
}

/// A rebuild over a store that recorded nothing installs an EMPTY Ready index, not a refusal: the
/// walk covered the whole retained range and found no change, which is a fact. The boundary it read
/// still stands, so a query below it is still refused.
BOOST_AUTO_TEST_CASE(rebuildOverAnEmptyStoreIsReadyNotUnavailable)
{
    HistoryMemStorage storage;
    StateHistoryStore store;
    bcos::task::syncWait(StateHistoryStore::writeRetentionBoundary(storage, 50));

    auto const report = bcos::task::syncWait(store.rebuild(storage));
    BOOST_CHECK_EQUAL(report.blocks, std::size_t{0});
    BOOST_CHECK_EQUAL(report.records, std::size_t{0});
    BOOST_CHECK(store.index().state() == IndexState::Ready);
    BOOST_CHECK(
        std::holds_alternative<HistoryUseCurrent>(readAt(store, storage, "k"sv, 60, 100, 100)));
    BOOST_CHECK_THROW(readAt(store, storage, "k"sv, 49, 100, 100), HistoryPruned);
}

/// Each way the shards can fail to add up, and the refusal it must produce. Every one of these
/// would otherwise leave some pre-image out of the index, and a missing version reads as "the key
/// never changed" — today's value under an old block's number (G6).
///
/// The store is left for the caller to mark unusable, which is what the startup path does; the
/// point here is that the rebuild THROWS rather than returning a short index.
BOOST_AUTO_TEST_CASE(anInconsistentShardTableRefusesToRebuild)
{
    auto const corruptThenRebuild = [](auto&& corrupt) {
        HistoryMemStorage storage;
        StateHistoryStore live;
        seedRebuildChain(live, storage);
        corrupt(storage);
        StateHistoryStore restarted;
        BOOST_CHECK_THROW(bcos::task::syncWait(restarted.rebuild(storage)), MPTInvariantViolation);
        // The startup path's response: refuse every query rather than serve a short index.
        restarted.markUnavailable();
        BOOST_CHECK_THROW(
            readAt(restarted, storage, rebuildKey(0), 12, 100, 100), HistoryIndexUnavailable);
    };

    // A block's meta row is gone, so its shard rows arrive with nothing declaring them.
    corruptThenRebuild(
        [](auto& storage) { deleteRow(storage, kStateHistory.shard, metaRowKey(12)); });

    // The meta row declares MORE shards than are there.
    corruptThenRebuild([](auto& storage) {
        auto const meta = decodeMeta(readRowValue(storage, kStateHistory.shard, metaRowKey(12)));
        overwriteRow(storage, kStateHistory.shard, metaRowKey(12),
            metaRowValue(meta.shardCount + 1, meta.recordCount, meta.blockHash));
    });

    // The meta row declares FEWER shards than are there.
    corruptThenRebuild([](auto& storage) {
        auto const meta = decodeMeta(readRowValue(storage, kStateHistory.shard, metaRowKey(12)));
        BOOST_REQUIRE_GT(meta.shardCount, 0U);
        overwriteRow(storage, kStateHistory.shard, metaRowKey(12),
            metaRowValue(meta.shardCount - 1, meta.recordCount, meta.blockHash));
    });

    // The meta row's record count disagrees with the records the shards hold.
    corruptThenRebuild([](auto& storage) {
        auto const meta = decodeMeta(readRowValue(storage, kStateHistory.shard, metaRowKey(12)));
        overwriteRow(storage, kStateHistory.shard, metaRowKey(12),
            metaRowValue(meta.shardCount, meta.recordCount + 1, meta.blockHash));
    });

    // A shard payload that ends inside a record.
    corruptThenRebuild([](auto& storage) {
        auto const payload = readRowValue(storage, kStateHistory.shard, shardRowKey(12, 0));
        overwriteRow(storage, kStateHistory.shard, shardRowKey(12, 0),
            payload.substr(0, payload.size() - 2));
    });

    // An unknown meta format version — the gate a future layout change goes through.
    corruptThenRebuild([](auto& storage) {
        auto value = metaRowValue(1, 0, blockHashOf(12));
        value[0] = '\x07';
        overwriteRow(storage, kStateHistory.shard, metaRowKey(12), std::move(value));
    });

    // A shard row whose ordinal skips ahead, so the 0..n-1 run has a hole.
    corruptThenRebuild([](auto& storage) {
        auto const payload = readRowValue(storage, kStateHistory.shard, shardRowKey(12, 0));
        overwriteRow(storage, kStateHistory.shard, shardRowKey(12, 9), payload);
        deleteRow(storage, kStateHistory.shard, shardRowKey(12, 0));
    });
}

/// G9: the index learns about a block only after its WriteBatch has landed. A merge that throws
/// leaves the StagedBlock unpublished, and the store must then answer exactly as it did before —
/// not from rows that are on disk but were never committed as far as the caller is concerned.
BOOST_AUTO_TEST_CASE(anUnpublishedBlockIsInvisibleToQueries)
{
    HistoryMemStorage storage;
    StateHistoryStore store;
    Diff at10;
    at10.change("k"sv, "old-at-9"sv);
    putBlock(store, storage, 10, at10);

    // Stage block 20 — the rows land, the index update is held back.
    Diff at20;
    at20.change("k"sv, "old-at-19"sv);
    auto staged = stageBlock(store, storage, 20, at20);
    BOOST_REQUIRE_EQUAL(rowsOfTable(storage, kStateHistory.shard).size(), std::size_t{4});

    // The merge "failed": nothing is published. Block 20 does not exist as far as queries go, and
    // block 19 reads as unchanged since block 10 rather than as block 20's pre-image.
    BOOST_CHECK(!store.recordedBlock(20));
    BOOST_CHECK_EQUAL(store.index().versionCount(), std::size_t{1});
    BOOST_CHECK(
        std::holds_alternative<HistoryUseCurrent>(readAt(store, storage, "k"sv, 19, 100, 100)));
    auto const at9 = readAt(store, storage, "k"sv, 9, 100, 100);
    BOOST_REQUIRE(std::holds_alternative<bcos::bytes>(at9));
    BOOST_CHECK_EQUAL(toText(std::get<bcos::bytes>(at9)), "old-at-9");

    // Publishing it afterwards is what makes it visible — so the invisibility above is the missing
    // publish and not a broken write.
    store.publish(std::move(staged), std::nullopt, std::nullopt);
    BOOST_CHECK(store.recordedBlock(20));
    auto const at19 = readAt(store, storage, "k"sv, 19, 100, 100);
    BOOST_REQUIRE(std::holds_alternative<bcos::bytes>(at19));
    BOOST_CHECK_EQUAL(toText(std::get<bcos::bytes>(at19)), "old-at-19");
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::history::test
