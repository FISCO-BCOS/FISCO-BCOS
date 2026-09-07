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
 * @file HistoryExpiryTest.cpp
 * @brief Expiry: nothing left behind, replayable from either interruption point, idempotent, and
 *        confined to the block it was asked to drop (spec B.5, G4)
 */

#include "HistoryTestHelpers.h"
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
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

BOOST_AUTO_TEST_SUITE(HistoryExpirySuite)

namespace
{
constexpr int kSeededKeyCount = 7;
/// A shard cap of 24 with 8-byte keys (record = 12 bytes) puts two keys per shard, so the
/// seven-key block below spans four shards and the sweep really has to walk them.
constexpr std::size_t kSmallShardCap = 24;

std::string seededKey(int index)
{
    return "key-" + std::to_string(index) + "!!!";  // 8 bytes
}

/// Seed one block with kSeededKeyCount changes, sharded.
void seedBlock(HistoryMemStorage& storage, bcos::protocol::BlockNumber block)
{
    Diff diff;
    for (int index = 0; index < kSeededKeyCount; ++index)
    {
        diff.change(seededKey(index), "old-at-" + std::to_string(block));
    }
    putBlock(storage, block, diff, kSmallShardCap);
}

/// Delete an index row behind the store's back, to stand in for a crash midway through an
/// interrupted deferred expiry.
void deleteIndexRow(
    HistoryMemStorage& storage, bcos::protocol::BlockNumber block, std::string_view key)
{
    auto keyBytes = makeBytes(key);
    bcos::task::syncWait(bcos::storage2::removeOne(
        storage, bcos::executor_v1::StateKey{kStateHistory.index, indexRowKey(keyBytes, block)}));
}
}  // namespace

/// After expiry the block leaves nothing behind in either table (spec B.5).
BOOST_AUTO_TEST_CASE(expireRemovesIndexAndManifest)
{
    HistoryMemStorage storage;
    seedBlock(storage, 10);
    BOOST_REQUIRE_EQUAL(rowsOfTable(storage, kStateHistory.index).size(), kSeededKeyCount);
    BOOST_REQUIRE_EQUAL(rowsOfTable(storage, kStateHistory.manifest).size(), 4);

    auto report = expireBlock(storage, 10);
    BOOST_CHECK_EQUAL(report.keyCount, kSeededKeyCount);
    BOOST_CHECK_EQUAL(report.indexDeletesIssued, kSeededKeyCount);
    BOOST_CHECK_EQUAL(report.manifestShardsDeleted, 4);

    BOOST_CHECK(rowsOfTable(storage, kStateHistory.index).empty());
    BOOST_CHECK(rowsOfTable(storage, kStateHistory.manifest).empty());
    BOOST_CHECK(keysOfBlock(storage, 10).empty());
}

/// Interruption point one: some index rows are already gone, the manifest is still there. The
/// manifest is what makes this recoverable — it still lists every key, so the replay re-issues
/// the whole set and the already-deleted ones are no-ops.
BOOST_AUTO_TEST_CASE(replayAfterPartialIndexDeletion)
{
    HistoryMemStorage storage;
    seedBlock(storage, 10);
    for (int index = 0; index < kSeededKeyCount / 2; ++index)
    {
        deleteIndexRow(storage, 10, seededKey(index));
    }
    BOOST_REQUIRE_EQUAL(
        rowsOfTable(storage, kStateHistory.index).size(), kSeededKeyCount - kSeededKeyCount / 2);

    auto report = expireBlock(storage, 10);
    // The manifest is intact, so the replay still sees every key.
    BOOST_CHECK_EQUAL(report.keyCount, kSeededKeyCount);
    BOOST_CHECK(rowsOfTable(storage, kStateHistory.index).empty());
    BOOST_CHECK(rowsOfTable(storage, kStateHistory.manifest).empty());
}

/// Interruption point two: every index row is gone but the manifest survives — the state a crash
/// leaves when expiry is deferred and the manifest is deleted last. The replay finishes the job.
BOOST_AUTO_TEST_CASE(replayAfterIndexDeletedButManifestKept)
{
    HistoryMemStorage storage;
    seedBlock(storage, 10);
    for (int index = 0; index < kSeededKeyCount; ++index)
    {
        deleteIndexRow(storage, 10, seededKey(index));
    }
    BOOST_REQUIRE(rowsOfTable(storage, kStateHistory.index).empty());
    BOOST_REQUIRE_EQUAL(rowsOfTable(storage, kStateHistory.manifest).size(), 4);

    auto report = expireBlock(storage, 10);
    BOOST_CHECK_EQUAL(report.keyCount, kSeededKeyCount);
    BOOST_CHECK_EQUAL(report.manifestShardsDeleted, 4);
    BOOST_CHECK(rowsOfTable(storage, kStateHistory.manifest).empty());
}

/// Whichever point it resumes from, expiry converges on the same final state, and running it
/// again on an already-expired block does nothing at all.
BOOST_AUTO_TEST_CASE(expireIsIdempotent)
{
    HistoryMemStorage clean;
    seedBlock(clean, 10);
    expireBlock(clean, 10);

    HistoryMemStorage interrupted;
    seedBlock(interrupted, 10);
    deleteIndexRow(interrupted, 10, seededKey(0));
    deleteIndexRow(interrupted, 10, seededKey(3));
    expireBlock(interrupted, 10);

    BOOST_CHECK(
        rowsOfTable(clean, kStateHistory.index) == rowsOfTable(interrupted, kStateHistory.index));
    BOOST_CHECK(rowsOfTable(clean, kStateHistory.manifest) ==
                rowsOfTable(interrupted, kStateHistory.manifest));

    auto second = expireBlock(clean, 10);
    BOOST_CHECK_EQUAL(second.keyCount, 0);
    BOOST_CHECK_EQUAL(second.indexDeletesIssued, 0);
    BOOST_CHECK_EQUAL(second.manifestShardsDeleted, 0);
    BOOST_CHECK(rowsOfTable(clean, kStateHistory.index).empty());
    BOOST_CHECK(rowsOfTable(clean, kStateHistory.manifest).empty());
}

/// G4, the asymmetry that matters: leaving a row behind is a wasted byte, deleting one row too
/// many silently destroys a neighbouring block's history. Blocks 10 and 11 change the same keys,
/// and expiring 10 must leave every one of block 11's rows — and its answers — untouched.
BOOST_AUTO_TEST_CASE(expireTouchesOnlyTheRequestedBlock)
{
    HistoryMemStorage storage;
    seedBlock(storage, 10);
    seedBlock(storage, 11);
    seedBlock(storage, 12);

    auto before = readAt(storage, seededKey(0), 10, 100, 100);
    BOOST_REQUIRE(std::holds_alternative<bcos::bytes>(before));
    BOOST_CHECK_EQUAL(toText(std::get<bcos::bytes>(before)), "old-at-11");

    expireBlock(storage, 11);

    // Blocks 10 and 12 keep every row.
    BOOST_CHECK_EQUAL(rowsOfTable(storage, kStateHistory.index).size(), 2 * kSeededKeyCount);
    BOOST_CHECK_EQUAL(rowsOfTable(storage, kStateHistory.manifest).size(), 8);
    BOOST_CHECK_EQUAL(keysOfBlock(storage, 10).size(), kSeededKeyCount);
    BOOST_CHECK_EQUAL(keysOfBlock(storage, 12).size(), kSeededKeyCount);
    BOOST_CHECK(keysOfBlock(storage, 11).empty());

    // And block 12's rows still answer queries — the deletion did not walk past its block.
    auto after = readAt(storage, seededKey(0), 11, 100, 100);
    BOOST_REQUIRE(std::holds_alternative<bcos::bytes>(after));
    BOOST_CHECK_EQUAL(toText(std::get<bcos::bytes>(after)), "old-at-12");
}

/// Expiring a block that was never written is a no-op, not an error: the sweep finds no manifest
/// and issues no deletes.
BOOST_AUTO_TEST_CASE(expireUnknownBlockDoesNothing)
{
    HistoryMemStorage storage;
    seedBlock(storage, 10);

    auto report = expireBlock(storage, 9);
    BOOST_CHECK_EQUAL(report.keyCount, 0);
    BOOST_CHECK_EQUAL(report.manifestShardsDeleted, 0);
    BOOST_CHECK_EQUAL(rowsOfTable(storage, kStateHistory.index).size(), kSeededKeyCount);
}

/// The two instantiations expire independently: dropping state history must not touch the trie
/// history of the same block (spec B.8 — same mechanism, separate key spaces, separate depths).
BOOST_AUTO_TEST_CASE(expiringStateHistoryLeavesTrieHistoryAlone)
{
    HistoryMemStorage storage;
    Diff diff;
    diff.change("shared"sv, "old"sv);
    bcos::task::syncWait(StateHistoryStore::put(storage, 10, diff.entries(), kWideShardCap));
    bcos::task::syncWait(TrieHistoryStore::put(storage, 10, diff.entries(), kWideShardCap));

    bcos::task::syncWait(StateHistoryStore::expire(storage, storage, 10));

    BOOST_CHECK(rowsOfTable(storage, kStateHistory.index).empty());
    BOOST_CHECK(rowsOfTable(storage, kStateHistory.manifest).empty());
    BOOST_CHECK_EQUAL(rowsOfTable(storage, kTrieHistory.index).size(), 1);
    BOOST_CHECK_EQUAL(rowsOfTable(storage, kTrieHistory.manifest).size(), 1);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::history::test
