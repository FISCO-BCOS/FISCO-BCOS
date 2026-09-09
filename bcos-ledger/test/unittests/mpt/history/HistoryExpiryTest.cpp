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
 * @brief Expiry: shardCount + 1 deletes and not one per key, idempotent on the logical-deletion
 *        layer, confined to the block it was asked to drop, and the retention boundary that moves
 *        with it (spec B.5, §13, G4)
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
#include <optional>
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
constexpr int kExpirySeededKeys = 7;
/// 8-byte keys with a 6-byte value make a 23-byte record, so a 50-byte cap puts two records per
/// shard and the seven-key block below spans four shards — the walk really has to iterate.
constexpr std::size_t kExpiryShardCap = 50;

std::string expirySeedKey(int index)
{
    return "key-" + std::to_string(index) + "!!!";  // 8 bytes
}

/// Seed one block with kExpirySeededKeys changes, sharded, and publish it.
void seedExpiryBlock(auto& store, auto& storage, bcos::protocol::BlockNumber block)
{
    Diff diff;
    for (int index = 0; index < kExpirySeededKeys; ++index)
    {
        diff.change(expirySeedKey(index), "old-" + std::to_string(block));  // 6-byte values
    }
    putBlock(store, storage, block, diff, kExpiryShardCap);
}
}  // namespace

// The idempotence cases below run against both deletion models, because the two are not
// interchangeable for this component and production uses the harder one.
//
//   HistoryMemStorage            ORDERED                      removeSome ERASES the row
//   HistoryLogicalDeleteStorage  ORDERED|LOGICAL_DELETION      removeSome leaves a sentinel the
//                                                              iterator still yields
//
// G3 puts expiry on the block's mutable layer, and that layer is the second kind:
// GlobalStateMutableStorage is MemoryStorage<StateKey, StateValue, ORDERED | LOGICAL_DELETION>
// (libinitializer/GlobalStateStorageInitializer.h:15-19). So on the real thing an expired block's
// own rows come back on the next walk, and "expire twice" only stays a no-op because the walk is
// told to skip them.

/// The saving the layout was reshaped for: one delete per shard plus one for the meta row, and NOT
/// one per key. The old layout issued keyCount + shardCount deletes for the same block, so this
/// count IS the change — it is asserted on the storage's own call counter rather than inferred
/// from the rows that disappeared.
BOOST_AUTO_TEST_CASE(expireDeletesShardsAndMetaAndNothingPerKey)
{
    CountingStorage backend;
    StateHistoryStore store;
    seedExpiryBlock(store, backend, 10);
    auto const before = rowsOfTable(backend.inner, kStateHistory.shard);
    auto const shardCount = decodeMeta(before[0].second).shardCount;
    BOOST_REQUIRE_EQUAL(shardCount, 4);
    BOOST_REQUIRE_EQUAL(before.size(), shardCount + 1);

    backend.removeCalls = 0;
    backend.removedKeys = 0;
    auto const report = bcos::task::syncWait(store.expire(backend, backend, 10));

    BOOST_CHECK_EQUAL(report.keyCount, std::size_t{kExpirySeededKeys});
    BOOST_CHECK_EQUAL(report.shardsDeleted, std::size_t{4});
    // One removeSome, carrying exactly shardCount + 1 row keys.
    BOOST_CHECK_EQUAL(backend.removeCalls, std::size_t{1});
    BOOST_CHECK_EQUAL(backend.removedKeys, std::size_t{shardCount + 1});
    BOOST_CHECK(rowsOfTable(backend.inner, kStateHistory.shard).empty());

    // The report carries what the in-memory index must now forget.
    BOOST_REQUIRE(report.retired.has_value());
    BOOST_CHECK_EQUAL(report.retired->block, 10);
    BOOST_CHECK_EQUAL(report.retired->keys.size(), std::size_t{kExpirySeededKeys});
    BOOST_CHECK_EQUAL(report.retired->shardsDeleted, std::size_t{4});
}

/// Publishing the retirement is what takes the block out of RAM, and it rides the next block's
/// publish because that is the only shape the commit path has. After it, the store neither
/// remembers the block nor answers the heights it used to answer.
BOOST_AUTO_TEST_CASE(publishingTheRetirementDropsTheBlockFromTheIndex)
{
    HistoryMemStorage storage;
    StateHistoryStore store;
    seedExpiryBlock(store, storage, 10);
    seedExpiryBlock(store, storage, 11);
    BOOST_REQUIRE_EQUAL(store.index().versionCount(), std::size_t{2 * kExpirySeededKeys});
    BOOST_REQUIRE(store.recordedBlock(10));

    auto const report = expireBlockAdvancing(store, storage, 10);
    Diff at12;
    at12.change("unrelated"sv, "v"sv);
    store.publish(stageBlock(store, storage, 12, at12), report.retired,
        std::optional<bcos::protocol::BlockNumber>{10});

    BOOST_CHECK(!store.recordedBlock(10));
    BOOST_CHECK(store.recordedBlock(11));
    // Block 10's versions are gone; block 11's and block 12's remain.
    BOOST_CHECK_EQUAL(store.index().versionCount(), std::size_t{kExpirySeededKeys + 1});
    BOOST_REQUIRE(store.index().boundary().has_value());
    BOOST_CHECK_EQUAL(*store.index().boundary(), 10);
    BOOST_CHECK_THROW(readAt(store, storage, expirySeedKey(0), 9, 100, 100), HistoryPruned);
    // Block 10 is still answerable, from block 11's surviving records.
    auto const at10 = readAt(store, storage, expirySeedKey(0), 10, 100, 100);
    BOOST_REQUIRE(std::holds_alternative<bcos::bytes>(at10));
    BOOST_CHECK_EQUAL(toText(std::get<bcos::bytes>(at10)), "old-11");
}

/// Running expiry again converges rather than erroring, on both deletion models. On the mutable
/// layer the block's own rows come back as sentinels, and skipping them is what makes the replay a
/// no-op: there is nothing left to clean up behind a row that is already deleted.
template <class Storage>
void checkExpireIsIdempotent()
{
    Storage storage;
    StateHistoryStore store;
    seedExpiryBlock(store, storage, 10);
    auto const first = bcos::task::syncWait(store.expire(storage, storage, 10));
    BOOST_CHECK_EQUAL(first.keyCount, std::size_t{kExpirySeededKeys});
    BOOST_CHECK(first.retired.has_value());

    auto const second = bcos::task::syncWait(store.expire(storage, storage, 10));
    BOOST_CHECK_EQUAL(second.keyCount, std::size_t{0});
    BOOST_CHECK_EQUAL(second.shardsDeleted, std::size_t{0});
    BOOST_CHECK(!second.retired.has_value());
    BOOST_CHECK(rowsOfTable(storage, kStateHistory.shard).empty());
}
BOOST_AUTO_TEST_CASE(expireIsIdempotent)
{
    checkExpireIsIdempotent<HistoryMemStorage>();
    checkExpireIsIdempotent<HistoryLogicalDeleteStorage>();
}

/// The two readings of the same sentinel row. expire() treats it as "already done" and skips it;
/// readBlock() treats it as "part of this block's diff is unreadable" and throws, because rollback
/// and the B.10 audits cannot use a short list. That is the whole reason the policy is a parameter
/// — and the sentinel is what production leaves behind, not a contrived state.
BOOST_AUTO_TEST_CASE(expiredRowIsSkippedByExpireAndRejectedByReadBlock)
{
    HistoryLogicalDeleteStorage storage;
    StateHistoryStore store;
    seedExpiryBlock(store, storage, 10);
    seedExpiryBlock(store, storage, 11);
    bcos::task::syncWait(store.expire(storage, storage, 10));

    BOOST_CHECK_THROW(readBlock(store, storage, 10), MPTInvariantViolation);
    // A block that was never written has no rows at all, so readBlock complains about the MISSING
    // meta row instead — a different failure with a different cause.
    BOOST_CHECK_THROW(readBlock(store, storage, 99), MPTInvariantViolation);
    // The untouched neighbour still reads back whole.
    BOOST_CHECK_EQUAL(readBlock(store, storage, 11).records.size(), std::size_t{kExpirySeededKeys});
}

/// readBlock verifies what the meta row promised. A shard that vanished, or a count that was
/// tampered with, would otherwise hand rollback a short diff and it would reverse-apply the wrong
/// state — so each mismatch is its own refusal.
BOOST_AUTO_TEST_CASE(readBlockRejectsCountsThatDisagreeWithTheMetaRow)
{
    HistoryMemStorage storage;
    StateHistoryStore store;
    seedExpiryBlock(store, storage, 10);
    BOOST_REQUIRE_EQUAL(
        readBlock(store, storage, 10).records.size(), std::size_t{kExpirySeededKeys});
    BOOST_CHECK_EQUAL(readBlock(store, storage, 10).meta.blockHash, blockHashOf(10));

    // One shard gone: the walk reads three where the meta row declares four.
    deleteRow(storage, kStateHistory.shard, shardRowKey(10, 3));
    BOOST_CHECK_THROW(readBlock(store, storage, 10), MPTInvariantViolation);

    // The meta row itself gone: nothing declares what to expect.
    HistoryMemStorage noMeta;
    StateHistoryStore noMetaStore;
    seedExpiryBlock(noMetaStore, noMeta, 10);
    deleteRow(noMeta, kStateHistory.shard, metaRowKey(10));
    BOOST_CHECK_THROW(readBlock(noMetaStore, noMeta, 10), MPTInvariantViolation);

    // A record count that lies, with every shard still present.
    HistoryMemStorage tampered;
    StateHistoryStore tamperedStore;
    seedExpiryBlock(tamperedStore, tampered, 10);
    overwriteRow(tampered, kStateHistory.shard, metaRowKey(10),
        metaRowValue(4, kExpirySeededKeys + 1, blockHashOf(10)));
    BOOST_CHECK_THROW(readBlock(tamperedStore, tampered, 10), MPTInvariantViolation);
}

/// G4, the asymmetry that matters: leaving a row behind is a wasted byte, deleting one row too many
/// silently destroys a neighbouring block's history. Blocks 10, 11 and 12 change the same keys, and
/// expiring 11 must leave every one of the others' rows — and their answers — untouched.
BOOST_AUTO_TEST_CASE(expireTouchesOnlyTheRequestedBlock)
{
    HistoryMemStorage storage;
    StateHistoryStore store;
    seedExpiryBlock(store, storage, 10);
    seedExpiryBlock(store, storage, 11);
    seedExpiryBlock(store, storage, 12);

    auto const before = readAt(store, storage, expirySeedKey(0), 10, 100, 100);
    BOOST_REQUIRE(std::holds_alternative<bcos::bytes>(before));
    BOOST_CHECK_EQUAL(toText(std::get<bcos::bytes>(before)), "old-11");

    expireBlock(store, storage, 11);

    // Ten rows per block survive for the other two: 4 shards + 1 meta, twice.
    BOOST_CHECK_EQUAL(rowsOfTable(storage, kStateHistory.shard).size(), std::size_t{10});
    BOOST_CHECK_EQUAL(readBlock(store, storage, 10).records.size(), std::size_t{kExpirySeededKeys});
    BOOST_CHECK_EQUAL(readBlock(store, storage, 12).records.size(), std::size_t{kExpirySeededKeys});
}

/// Expiring a block that was never written is a no-op, not an error: the walk finds nothing, issues
/// no deletes and reports nothing retired.
BOOST_AUTO_TEST_CASE(expireUnknownBlockDoesNothing)
{
    CountingStorage backend;
    StateHistoryStore store;
    seedExpiryBlock(store, backend, 10);
    backend.removeCalls = 0;

    auto const report = bcos::task::syncWait(store.expire(backend, backend, 9));
    BOOST_CHECK_EQUAL(report.keyCount, std::size_t{0});
    BOOST_CHECK_EQUAL(report.shardsDeleted, std::size_t{0});
    BOOST_CHECK(!report.retired.has_value());
    BOOST_CHECK_EQUAL(backend.removeCalls, std::size_t{0});
    BOOST_CHECK_EQUAL(rowsOfTable(backend.inner, kStateHistory.shard).size(), std::size_t{5});
}

/// The two instantiations expire independently: dropping state history must not touch the trie
/// history of the same block (spec B.8 — same mechanism, separate key spaces, separate depths).
BOOST_AUTO_TEST_CASE(expiringStateHistoryLeavesTrieHistoryAlone)
{
    HistoryMemStorage storage;
    StateHistoryStore stateStore;
    TrieHistoryStore trieStore;
    Diff diff;
    diff.change("shared"sv, "old"sv);
    putBlock(stateStore, storage, 10, diff);
    putBlock(trieStore, storage, 10, diff);

    bcos::task::syncWait(stateStore.expire(storage, storage, 10));

    BOOST_CHECK(rowsOfTable(storage, kStateHistory.shard).empty());
    BOOST_CHECK_EQUAL(rowsOfTable(storage, kTrieHistory.shard).size(), std::size_t{2});
    BOOST_CHECK(!boundaryOnDisk(trieStore, storage).has_value());
}

/// The boundary policy, all three readings on the same input. "Discard block E's history" has two
/// callers moving in opposite directions along the chain, and only one of them changes which block
/// is the OLDEST answerable — so the policy is an argument, not a rule baked into expire, and its
/// default is the conservative one. Advance takes a MAX, because the block an expiry names is not
/// monotonic across callers.
BOOST_AUTO_TEST_CASE(expireBoundaryPolicyKeepsOrAdvancesByMaximum)
{
    {
        HistoryMemStorage storage;
        StateHistoryStore store;
        seedExpiryBlock(store, storage, 10);
        seedExpiryBlock(store, storage, 11);
        // Absent before, and Keep must leave it absent — not "advance from nothing to E".
        BOOST_REQUIRE(!boundaryOnDisk(store, storage).has_value());
        expireBlock(store, storage, 11);  // default policy
        BOOST_CHECK(!boundaryOnDisk(store, storage).has_value());
        // The deletes still happened: Keep is about the metadata, not about the walk.
        BOOST_CHECK_EQUAL(rowsOfTable(storage, kStateHistory.shard).size(), std::size_t{5});
    }
    {
        HistoryMemStorage storage;
        StateHistoryStore store;
        seedExpiryBlock(store, storage, 10);
        // Present before, and Keep must not move it either.
        bcos::task::syncWait(StateHistoryStore::writeRetentionBoundary(storage, 9));
        expireBlock(store, storage, 10);
        BOOST_CHECK_EQUAL(*boundaryOnDisk(store, storage), 9);
    }
    {
        HistoryMemStorage storage;
        StateHistoryStore store;
        seedExpiryBlock(store, storage, 10);
        seedExpiryBlock(store, storage, 11);
        seedExpiryBlock(store, storage, 12);
        bcos::task::syncWait(StateHistoryStore::writeRetentionBoundary(storage, 11));

        // A LOWER block, with Advance: the max keeps 11. An operator raising the retention depth
        // makes N - H jump backwards, and assigning here would claim block 10 is intact when its
        // records were already deleted.
        expireBlockAdvancing(store, storage, 5);
        BOOST_CHECK_EQUAL(*boundaryOnDisk(store, storage), 11);
        // The same block: still 11, not a rewrite that could drift.
        expireBlockAdvancing(store, storage, 11);
        BOOST_CHECK_EQUAL(*boundaryOnDisk(store, storage), 11);
        // A higher block does move it — the max is a floor, not a freeze.
        expireBlockAdvancing(store, storage, 12);
        BOOST_CHECK_EQUAL(*boundaryOnDisk(store, storage), 12);
    }
}

/// The shape an operational rollback walks (PR-D's HistoryRollback): descend from the tip,
/// reverse-apply block N by reading each key at N-1, then discard N's record — and keep going
/// downwards. Every step reads BELOW the block it just discarded, so advancing the boundary to the
/// discarded block would refuse the walk's own next read. The default policy is what makes this
/// terminate rather than throw HistoryPruned on step two.
BOOST_AUTO_TEST_CASE(rollbackShapeDiscardsFromTheTopWithoutRefusingItself)
{
    HistoryMemStorage storage;
    StateHistoryStore store;
    seedExpiryBlock(store, storage, 10);
    seedExpiryBlock(store, storage, 11);
    seedExpiryBlock(store, storage, 12);
    // The commit path's seed: history starts at block 10, so block 9 is the oldest answerable.
    bcos::task::syncWait(StateHistoryStore::writeRetentionBoundary(storage, 9));

    constexpr bcos::protocol::BlockNumber kTip = 12;
    constexpr bcos::protocol::BlockNumber kDepth = 100;
    for (bcos::protocol::BlockNumber block = kTip; block >= 10; --block)
    {
        // Reverse-apply: what the key held at block - 1, from block's own recorded pre-image.
        auto const before = readAt(store, storage, expirySeedKey(0), block - 1, kTip, kDepth);
        BOOST_REQUIRE_MESSAGE(std::holds_alternative<bcos::bytes>(before),
            "block " << block - 1 << " must answer from block " << block << "'s pre-image");
        BOOST_CHECK_EQUAL(toText(std::get<bcos::bytes>(before)), "old-" + std::to_string(block));
        // ...then discard the block just applied. Keep, because this is the NEWEST record going.
        expireBlock(store, storage, block);
    }

    // The boundary never moved, so the walk was never refused by its own progress.
    BOOST_CHECK_EQUAL(*boundaryOnDisk(store, storage), 9);
    BOOST_CHECK(rowsOfTable(storage, kStateHistory.shard).empty());
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::history::test
