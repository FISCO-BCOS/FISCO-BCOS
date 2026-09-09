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
 * @file HistoryAuditTest.cpp
 * @brief spec B.10's five checks over the optimized layout, one injected corruption each
 */

#include "../history/HistoryTestHelpers.h"
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/mpt/Errors.h>
#include <bcos-ledger/mpt/audit/HistoryAudit.h>
#include <bcos-ledger/mpt/history/HistoryCommit.h>
#include <bcos-ledger/mpt/history/HistoryRowCodec.h>
#include <bcos-ledger/mpt/history/HistoryTables.h>
#include <bcos-ledger/mpt/history/ReverseHistoryStore.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

using namespace std::string_view_literals;

namespace bcos::ledger::mpt::audit::test
{

BOOST_AUTO_TEST_SUITE(HistoryAuditSuite)

/// How many findings of @p kind a report carries.
std::size_t countFindings(HistoryAuditReport const& report, HistoryFindingKind kind)
{
    return static_cast<std::size_t>(std::count_if(report.findings.begin(), report.findings.end(),
        [kind](HistoryFinding const& finding) { return finding.kind == kind; }));
}

/// The first finding of @p kind. BOOST_REQUIREs that one exists, so a caller can read its detail.
HistoryFinding const& findingOf(HistoryAuditReport const& report, HistoryFindingKind kind)
{
    auto const found = std::find_if(report.findings.begin(), report.findings.end(),
        [kind](HistoryFinding const& finding) { return finding.kind == kind; });
    BOOST_REQUIRE(found != report.findings.end());
    return *found;
}

/// The ledger side of B.10 ⑤: the hash the commit path recorded for each block. The fixtures write
/// their history through the ordinary put(), which stamps `blockHashOf(block)` into the meta row,
/// so a healthy store is one where the ledger agrees with exactly that.
BlockHashSource ledgerHashes()
{
    return [](protocol::BlockNumber block) -> std::optional<bcos::h256> {
        return history::test::blockHashOf(block);
    };
}

/// Six blocks of state history, 5..10, two keys each. The chain tip is 10 and the retention depth
/// is 6, so the window is exactly [5, 10] — every B.10 check has something to agree with.
///
/// Block 5 also seeds the retention boundary the way the commit path does — seedRetentionBoundary
/// with no prior value, which writes firstRecorded - 1 — so the store is shaped like one a running
/// node produced rather than like shards with no metadata behind them.
struct HistoryAuditFixture
{
    history::StateHistoryStore store;
    history::test::HistoryMemStorage storage;
    static constexpr protocol::BlockNumber kTip = 10;
    static constexpr protocol::BlockNumber kDepth = 6;
    static constexpr protocol::BlockNumber kOldest = 5;

    HistoryAuditFixture()
    {
        for (auto block = kOldest; block <= kTip; ++block)
        {
            history::test::Diff diff;
            diff.change("alpha"sv, "alpha-before-" + std::to_string(block));
            diff.change("beta"sv, std::nullopt);
            history::test::putBlock(store, storage, block, diff);
        }
        bcos::task::syncWait(history::seedRetentionBoundary<history::StateHistoryStore>(
            storage, std::nullopt, kOldest));
    }

    [[nodiscard]] HistoryAuditReport audit(
        protocol::BlockNumber depth = kDepth, protocol::BlockNumber tip = kTip)
    {
        return bcos::task::syncWait(auditStateHistory(storage, tip, depth, 0, ledgerHashes()));
    }
};

BOOST_AUTO_TEST_CASE(cleanWindowPasses)
{
    HistoryAuditFixture fixture;
    auto const report = fixture.audit();

    BOOST_CHECK(report.consistent());
    BOOST_CHECK_NO_THROW(report.throwIfInconsistent());
    BOOST_CHECK_EQUAL(report.windowStart, HistoryAuditFixture::kOldest);
    BOOST_CHECK_EQUAL(report.oldestRetained, HistoryAuditFixture::kOldest);
    BOOST_CHECK_EQUAL(report.newestRetained, HistoryAuditFixture::kTip);
    BOOST_CHECK_EQUAL(report.blocksWithMeta, 6);
    // One shard per block at the wide cap, two records each.
    BOOST_CHECK_EQUAL(report.shardRows, 6);
    BOOST_CHECK_EQUAL(report.records, 12);
    BOOST_CHECK_EQUAL(report.declaredRecords, 12);
    // boundary == oldest meta row - 1, the shape seedRetentionBoundary leaves behind.
    BOOST_REQUIRE(report.retentionBoundary.has_value());
    BOOST_CHECK_EQUAL(*report.retentionBoundary, HistoryAuditFixture::kOldest - 1);

    // B.10 ③: the rebuild a restarting node performs, run here and accounted for.
    BOOST_CHECK(report.rebuilt);
    BOOST_CHECK_EQUAL(report.rebuiltBlocks, 6);
    BOOST_CHECK_EQUAL(report.rebuiltRecords, 12);
    // versionCount == Σ recordCount over the blocks the rebuild is accountable for, which here is
    // every block: the boundary sits one below the oldest.
    BOOST_CHECK_EQUAL(report.indexVersions, 12);
    BOOST_CHECK_EQUAL(report.accountableRecords, 12);
    BOOST_CHECK_GT(report.bytesScanned, 0U);

    // B.10 ⑤ actually ran.
    BOOST_CHECK(report.blockHashChecked);
}

/// Without a hash source the audit still runs, and says out loud that ⑤ did not.
BOOST_AUTO_TEST_CASE(noHashSourceLeavesTheChainIdentityUnchecked)
{
    HistoryAuditFixture fixture;
    auto const report = bcos::task::syncWait(
        auditStateHistory(fixture.storage, HistoryAuditFixture::kTip, HistoryAuditFixture::kDepth));

    BOOST_CHECK(report.consistent());
    BOOST_CHECK(!report.blockHashChecked);
}

/// The same generic component, second instantiation (spec B.8): the audit is written once and
/// must work for the trie-node history as well.
BOOST_AUTO_TEST_CASE(trieHistoryUsesTheSameAudit)
{
    history::TrieHistoryStore store;
    history::test::HistoryMemStorage storage;
    for (protocol::BlockNumber block = 1; block <= 3; ++block)
    {
        history::test::Diff diff;
        diff.change("/mptp/a:\x00"sv, "node-before-" + std::to_string(block));
        history::test::putBlock(store, storage, block, diff);
    }
    bcos::task::syncWait(
        history::seedRetentionBoundary<history::TrieHistoryStore>(storage, std::nullopt, 1));

    auto const report = bcos::task::syncWait(auditTrieHistory(storage, 3, 3, 0, ledgerHashes()));
    BOOST_CHECK(report.consistent());
    BOOST_CHECK_EQUAL(report.blocksWithMeta, 3);
    BOOST_CHECK_EQUAL(report.indexVersions, 3);
}

/// NEGATIVE CONTROL — B.10 ①, the shard count. The meta row promises a shard that is not there,
/// so part of block 7's diff is unreadable and no rebuild can index it.
BOOST_AUTO_TEST_CASE(metaShardCountAboveWhatIsOnDiskIsReportedAsBOne)
{
    HistoryAuditFixture fixture;
    history::test::overwriteRow(fixture.storage, history::kStateHistory.shard,
        history::metaRowKey(7), history::metaRowValue(2, 2, history::test::blockHashOf(7)));

    auto const report = fixture.audit();
    BOOST_CHECK(!report.consistent());
    BOOST_REQUIRE_EQUAL(countFindings(report, HistoryFindingKind::MetaShardCountMismatch), 1);
    BOOST_CHECK_EQUAL(findingOf(report, HistoryFindingKind::MetaShardCountMismatch).block, 7);
    BOOST_CHECK(findingOf(report, HistoryFindingKind::MetaShardCountMismatch)
                    .detail.find("declares 2 shards, 1 are on disk") != std::string::npos);
    // The rebuild refuses over the same damage, and says so as its own finding — at the block it
    // tripped at, taken off the exception rather than guessed from the oldest retained block.
    BOOST_CHECK(!report.rebuilt);
    BOOST_REQUIRE_EQUAL(countFindings(report, HistoryFindingKind::RebuildRejected), 1);
    BOOST_CHECK_EQUAL(findingOf(report, HistoryFindingKind::RebuildRejected).block, 7);
    BOOST_CHECK_THROW(report.throwIfInconsistent(), MPTInvariantViolation);
}

/// NEGATIVE CONTROL — B.10 ①, a deleted shard. The other way round from the case above: the meta
/// row is untouched and the shard is gone.
BOOST_AUTO_TEST_CASE(deletedShardIsReportedAsBOne)
{
    HistoryAuditFixture fixture;
    history::test::deleteRow(
        fixture.storage, history::kStateHistory.shard, history::shardRowKey(7, 0));

    auto const report = fixture.audit();
    BOOST_REQUIRE_EQUAL(countFindings(report, HistoryFindingKind::MetaShardCountMismatch), 1);
    BOOST_CHECK_EQUAL(findingOf(report, HistoryFindingKind::MetaShardCountMismatch).block, 7);
    BOOST_CHECK_EQUAL(countFindings(report, HistoryFindingKind::MetaRecordCountMismatch), 1);
    BOOST_CHECK(!report.rebuilt);
}

/// NEGATIVE CONTROL — B.10 ①, the record count. The shards are all there and decode cleanly; the
/// meta row simply says a different number, which is what a truncated write leaves behind.
BOOST_AUTO_TEST_CASE(metaRecordCountDisagreeingWithTheShardsIsReportedAsBOne)
{
    HistoryAuditFixture fixture;
    history::test::overwriteRow(fixture.storage, history::kStateHistory.shard,
        history::metaRowKey(8), history::metaRowValue(1, 3, history::test::blockHashOf(8)));

    auto const report = fixture.audit();
    BOOST_REQUIRE_EQUAL(countFindings(report, HistoryFindingKind::MetaRecordCountMismatch), 1);
    BOOST_CHECK_EQUAL(findingOf(report, HistoryFindingKind::MetaRecordCountMismatch).block, 8);
    BOOST_CHECK(
        findingOf(report, HistoryFindingKind::MetaRecordCountMismatch)
            .detail.find("declares 3 records, the shards decode to 2") != std::string::npos);
    BOOST_CHECK_EQUAL(countFindings(report, HistoryFindingKind::MetaShardCountMismatch), 0);
    BOOST_CHECK(!report.rebuilt);
}

/// NEGATIVE CONTROL — B.10 ①, the shard ordinals. A block whose records were split across three
/// shards, with the first one gone: the run no longer starts at 0, and the rebuild walk stops at
/// the gap rather than skipping past it.
BOOST_AUTO_TEST_CASE(shardOrdinalGapIsReportedAsBOne)
{
    history::StateHistoryStore store;
    history::test::HistoryMemStorage storage;
    history::test::Diff diff;
    diff.change("alpha"sv, "one"sv);
    diff.change("beta"sv, "two"sv);
    diff.change("gamma"sv, "three"sv);
    // A cap of one byte puts every record in its own shard: the payload is non-empty after the
    // first record, so the next one always trips the cap.
    history::test::putBlock(store, storage, 1, diff, /*shardCap=*/1);
    bcos::task::syncWait(
        history::seedRetentionBoundary<history::StateHistoryStore>(storage, std::nullopt, 1));

    auto const clean = bcos::task::syncWait(auditStateHistory(storage, 1, 1, 0, ledgerHashes()));
    BOOST_REQUIRE(clean.consistent());
    BOOST_CHECK_EQUAL(clean.shardRows, 3);

    history::test::deleteRow(storage, history::kStateHistory.shard, history::shardRowKey(1, 0));
    auto const report = bcos::task::syncWait(auditStateHistory(storage, 1, 1, 0, ledgerHashes()));

    BOOST_REQUIRE_EQUAL(countFindings(report, HistoryFindingKind::ShardOrdinalGap), 1);
    BOOST_CHECK_EQUAL(findingOf(report, HistoryFindingKind::ShardOrdinalGap).block, 1);
    BOOST_CHECK(
        findingOf(report, HistoryFindingKind::ShardOrdinalGap).detail.find("shard 0 is missing") !=
        std::string::npos);
    BOOST_CHECK(!report.rebuilt);
}

/// NEGATIVE CONTROL — B.10 ② and ③ together: a block inside the window has no meta row. Its shard
/// is still on disk, which is what makes the rebuild refuse the WHOLE store — a shard row that no
/// meta row introduced cannot be attributed to a block.
BOOST_AUTO_TEST_CASE(missingMetaInsideTheWindowIsReportedAsBTwoAndStopsTheRebuild)
{
    HistoryAuditFixture fixture;
    history::test::deleteRow(fixture.storage, history::kStateHistory.shard, history::metaRowKey(7));

    auto const report = fixture.audit();
    BOOST_REQUIRE_EQUAL(countFindings(report, HistoryFindingKind::MissingMeta), 1);
    BOOST_CHECK_EQUAL(findingOf(report, HistoryFindingKind::MissingMeta).block, 7);
    BOOST_CHECK(findingOf(report, HistoryFindingKind::MissingMeta)
                    .detail.find("shard rows exist for this block but it has no meta row") !=
                std::string::npos);

    BOOST_CHECK(!report.rebuilt);
    BOOST_REQUIRE_EQUAL(countFindings(report, HistoryFindingKind::RebuildRejected), 1);
    BOOST_CHECK(findingOf(report, HistoryFindingKind::RebuildRejected)
                    .detail.find("not preceded by its block's meta row") != std::string::npos);
    // Block 7, not block 5: the finding names the height the rebuild tripped at.
    BOOST_CHECK_EQUAL(findingOf(report, HistoryFindingKind::RebuildRejected).block, 7);
    BOOST_CHECK_NE(
        findingOf(report, HistoryFindingKind::RebuildRejected).block, report.oldestRetained);
}

/// A block that lost BOTH its rows is a plain ② hole: nothing is left to refuse a rebuild over,
/// so the store still rebuilds — and that is exactly why ② has to be checked separately. The
/// index a restart builds is perfectly well-formed and simply cannot answer for block 7.
BOOST_AUTO_TEST_CASE(wholeBlockGoneIsAHoleThatStillRebuilds)
{
    HistoryAuditFixture fixture;
    history::test::deleteRow(fixture.storage, history::kStateHistory.shard, history::metaRowKey(7));
    history::test::deleteRow(
        fixture.storage, history::kStateHistory.shard, history::shardRowKey(7, 0));

    auto const report = fixture.audit();
    BOOST_CHECK(report.rebuilt);
    BOOST_REQUIRE_EQUAL(countFindings(report, HistoryFindingKind::MissingMeta), 1);
    BOOST_CHECK_EQUAL(findingOf(report, HistoryFindingKind::MissingMeta).block, 7);
    BOOST_CHECK_EQUAL(report.blocksWithMeta, 5);
}

/// NEGATIVE CONTROL — B.10 ④: what is on disk must agree with the depth the operator believes
/// the node runs with. Here the store retains six blocks while H says three.
BOOST_AUTO_TEST_CASE(retainedSpanDisagreeingWithTheDepthIsReportedAsBFour)
{
    HistoryAuditFixture fixture;
    auto const report = fixture.audit(/*depth=*/3);

    BOOST_CHECK_EQUAL(report.windowStart, 8);
    BOOST_CHECK_EQUAL(report.oldestRetained, HistoryAuditFixture::kOldest);
    BOOST_REQUIRE_EQUAL(report.findings.size(), 1);
    BOOST_CHECK(report.findings.front().kind == HistoryFindingKind::RetentionBoundaryMismatch);
    BOOST_CHECK_EQUAL(report.findings.front().block, HistoryAuditFixture::kOldest);
}

/// The other half of ④: history that stops short of the tip. A node that crashed before writing
/// the newest block's history looks exactly like this.
BOOST_AUTO_TEST_CASE(historyThatStopsBelowTheTipIsReportedAsBFour)
{
    HistoryAuditFixture fixture;
    auto const report = fixture.audit(/*depth=*/7, /*tip=*/11);

    BOOST_CHECK_EQUAL(report.windowStart, HistoryAuditFixture::kOldest);
    BOOST_CHECK_EQUAL(report.newestRetained, HistoryAuditFixture::kTip);
    BOOST_CHECK_EQUAL(countFindings(report, HistoryFindingKind::RetentionBoundaryMismatch), 1);
    // Block 11 is inside the window and has no meta row, so ② fires for it too — the two checks
    // see the same damage from different angles and both are worth reporting.
    BOOST_CHECK_EQUAL(countFindings(report, HistoryFindingKind::MissingMeta), 1);
}

/// A node that turned history on part-way through its life has no meta rows for the blocks before
/// that, and those are not holes.
BOOST_AUTO_TEST_CASE(firstHistoryBlockMovesTheWindowStart)
{
    HistoryAuditFixture fixture;
    auto const noisy =
        bcos::task::syncWait(auditStateHistory(fixture.storage, 10, 20, 0, ledgerHashes()));
    // Blocks 0..4 are one gap, so one finding — not five. An era that predates the feature is the
    // common shape here, and a finding per block would bury the other four checks.
    BOOST_REQUIRE_EQUAL(countFindings(noisy, HistoryFindingKind::MissingMeta), 1);
    auto const& gap = findingOf(noisy, HistoryFindingKind::MissingMeta);
    BOOST_CHECK_EQUAL(gap.block, 0);
    BOOST_CHECK(gap.detail.find("blocks 0..4 have no meta row (5 blocks)") != std::string::npos);

    auto const quiet =
        bcos::task::syncWait(auditStateHistory(fixture.storage, 10, 20, 5, ledgerHashes()));
    BOOST_CHECK(quiet.consistent());
}

/// The boundary is what a SERVING query consults — the rebuild seeds the index's boundary from
/// that row — so an audit that never reads it can call a store healthy while every historical read
/// below the oldest block answers with today's value. The invariant in every regime is
/// boundary == max(0, oldest meta row - 1).

/// NEGATIVE CONTROL — a shard row that is a DELETION SENTINEL rather than bytes, the shape an
/// expiry leaves on a mutable layer before it is merged down (HistoryLogicalDeleteStorage).
///
/// The two walks read the same row in opposite ways on purpose, and this pins both: the audit's
/// scan skips it — the row is already gone, so the block is short a shard and ① says so — while
/// the rebuild refuses over it, because a sentinel means records it cannot index. An audit that
/// followed the rebuild here would report nothing at all about the block; one that ignored the
/// rebuild would not say the store is unservable.
BOOST_AUTO_TEST_CASE(deletionSentinelShardIsSkippedByTheScanAndRefusedByTheRebuild)
{
    history::StateHistoryStore store;
    history::test::HistoryLogicalDeleteStorage storage;
    for (protocol::BlockNumber block = 5; block <= 7; ++block)
    {
        history::test::Diff diff;
        diff.change("alpha"sv, "alpha-before-" + std::to_string(block));
        diff.change("beta"sv, std::nullopt);
        history::test::putBlock(store, storage, block, diff);
    }
    bcos::task::syncWait(
        history::seedRetentionBoundary<history::StateHistoryStore>(storage, std::nullopt, 5));

    auto const clean = bcos::task::syncWait(auditStateHistory(storage, 7, 3, 0, ledgerHashes()));
    BOOST_REQUIRE(clean.consistent());

    // removeOne on this backend marks the row instead of erasing it, so the iterator still yields
    // it — as a sentinel.
    history::test::deleteRow(storage, history::kStateHistory.shard, history::shardRowKey(6, 0));

    auto const report = bcos::task::syncWait(auditStateHistory(storage, 7, 3, 0, ledgerHashes()));
    BOOST_CHECK_EQUAL(report.shardRows, 2);  // the sentinel was skipped, not counted
    BOOST_REQUIRE_EQUAL(countFindings(report, HistoryFindingKind::MetaShardCountMismatch), 1);
    BOOST_CHECK_EQUAL(findingOf(report, HistoryFindingKind::MetaShardCountMismatch).block, 6);
    BOOST_CHECK(findingOf(report, HistoryFindingKind::MetaShardCountMismatch)
                    .detail.find("declares 1 shards, 0 are on disk") != std::string::npos);
    BOOST_CHECK_EQUAL(countFindings(report, HistoryFindingKind::MetaRecordCountMismatch), 1);

    BOOST_CHECK(!report.rebuilt);
    BOOST_REQUIRE_EQUAL(countFindings(report, HistoryFindingKind::RebuildRejected), 1);
    auto const& refusal = findingOf(report, HistoryFindingKind::RebuildRejected);
    BOOST_CHECK(refusal.detail.find("deletion sentinel") != std::string::npos);
    // The block comes off the exception, so it names the damaged height rather than the oldest.
    BOOST_CHECK_EQUAL(refusal.block, 6);
}

/// NEGATIVE CONTROL — B.10 ④, (a): meta rows with no boundary row behind them.
BOOST_AUTO_TEST_CASE(boundaryRowMissingWhileMetaRowsExistIsReportedAsBFour)
{
    HistoryAuditFixture fixture;
    history::test::deleteRow(fixture.storage, history::kStateHistory.boundary,
        std::string(history::kRetentionBoundaryRowKey));

    auto const report = fixture.audit();
    BOOST_CHECK(!report.retentionBoundary.has_value());
    BOOST_REQUIRE_EQUAL(countFindings(report, HistoryFindingKind::RetentionBoundaryRowMismatch), 1);
    BOOST_CHECK(findingOf(report, HistoryFindingKind::RetentionBoundaryRowMismatch)
                    .detail.find("no retention-boundary row") != std::string::npos);
}

/// NEGATIVE CONTROL — B.10 ④, (b) in the DANGEROUS direction: the row claims heights whose shards
/// are gone. A read at block 2 would find no version and answer HistoryUseCurrent — today's value
/// under block 2's number — because the index's boundary said block 2 was covered.
BOOST_AUTO_TEST_CASE(boundaryBelowTheOldestBlockIsReportedAsBFour)
{
    HistoryAuditFixture fixture;
    bcos::task::syncWait(history::StateHistoryStore::writeRetentionBoundary(fixture.storage, 1));

    auto const report = fixture.audit();
    BOOST_REQUIRE_EQUAL(countFindings(report, HistoryFindingKind::RetentionBoundaryRowMismatch), 1);
    auto const& finding = findingOf(report, HistoryFindingKind::RetentionBoundaryRowMismatch);
    BOOST_CHECK_EQUAL(finding.block, 1);
    BOOST_CHECK(
        finding.detail.find("claims heights this store no longer holds") != std::string::npos);
    BOOST_CHECK(finding.detail.find("TODAY's value") != std::string::npos);
    // The rebuild starts at 2 and still reaches every block.
    BOOST_CHECK(report.rebuilt);
    BOOST_CHECK_EQUAL(report.indexVersions, 12);
    BOOST_CHECK_EQUAL(report.accountableRecords, 12);
}

/// NEGATIVE CONTROL — B.10 ④, (b) the other way: the row sits above the oldest block on disk, so
/// the rebuild walk starts past three blocks whose records are still there. Wasteful rather than
/// wrong — those blocks are refused by the boundary check anyway — and the report shows the waste
/// as the gap between what is on disk and what the index can answer from.
BOOST_AUTO_TEST_CASE(boundaryAboveTheOldestBlockIsReportedAsBFour)
{
    HistoryAuditFixture fixture;
    bcos::task::syncWait(history::StateHistoryStore::writeRetentionBoundary(fixture.storage, 7));

    auto const report = fixture.audit();
    BOOST_REQUIRE_EQUAL(countFindings(report, HistoryFindingKind::RetentionBoundaryRowMismatch), 1);
    auto const& boundaryFinding =
        findingOf(report, HistoryFindingKind::RetentionBoundaryRowMismatch);
    BOOST_CHECK_EQUAL(boundaryFinding.block, 7);
    BOOST_CHECK(boundaryFinding.detail.find("dead weight") != std::string::npos);

    // Blocks 5, 6 and 7 — six of the twelve records — are below the rebuild's start, so the index
    // a restart builds holds the other six and cannot answer from those.
    BOOST_CHECK(report.rebuilt);
    BOOST_CHECK_EQUAL(report.declaredRecords, 12);
    BOOST_CHECK_EQUAL(report.accountableRecords, 6);
    BOOST_CHECK_EQUAL(report.indexVersions, 6);
}

/// NEGATIVE CONTROL — B.10 ⑤: the pre-images retained for a height were captured on a different
/// block. Reverse-applying them would move the state plane onto a chain this node is not on, so
/// the identity of every retained block is checked before an operator is allowed to trust them.
BOOST_AUTO_TEST_CASE(metaBlockHashDisagreeingWithTheLedgerIsReportedAsBFive)
{
    HistoryAuditFixture fixture;
    // Same shard and record counts, a different chain.
    history::test::overwriteRow(fixture.storage, history::kStateHistory.shard,
        history::metaRowKey(9), history::metaRowValue(1, 2, history::test::blockHashOf(9999)));

    auto const report = fixture.audit();
    BOOST_REQUIRE_EQUAL(countFindings(report, HistoryFindingKind::BlockHashMismatch), 1);
    BOOST_CHECK_EQUAL(findingOf(report, HistoryFindingKind::BlockHashMismatch).block, 9);
    BOOST_CHECK(findingOf(report, HistoryFindingKind::BlockHashMismatch)
                    .detail.find("captured on a different block") != std::string::npos);
    // Nothing else is wrong: the layout checks pass and the rebuild still succeeds.
    BOOST_CHECK(report.rebuilt);
    BOOST_CHECK_EQUAL(report.findings.size(), 1);
}

/// A zero block hash is not a free pass. A store whose meta rows were written by something that
/// did not know the block hash reads as a mismatch, the same as any other wrong value — the audit
/// has no "unset" value to recognise, and inventing one would let a whole class of stores through.
BOOST_AUTO_TEST_CASE(zeroMetaBlockHashIsAMismatchLikeAnyOther)
{
    HistoryAuditFixture fixture;
    history::test::overwriteRow(fixture.storage, history::kStateHistory.shard,
        history::metaRowKey(6), history::metaRowValue(1, 2, bcos::h256{}));

    auto const report = fixture.audit();
    BOOST_REQUIRE_EQUAL(countFindings(report, HistoryFindingKind::BlockHashMismatch), 1);
    BOOST_CHECK_EQUAL(findingOf(report, HistoryFindingKind::BlockHashMismatch).block, 6);
}

/// NEGATIVE CONTROL — B.10 ⑤, unproven rather than wrong: the ledger has no hash at a height that
/// has a meta row. "Could not compare" must not read as "the hashes agree" (G6).
BOOST_AUTO_TEST_CASE(blockWithNoLedgerHashIsReportedAsUnverifiable)
{
    HistoryAuditFixture fixture;
    auto const report = bcos::task::syncWait(
        auditStateHistory(fixture.storage, HistoryAuditFixture::kTip, HistoryAuditFixture::kDepth,
            0, [](protocol::BlockNumber block) -> std::optional<bcos::h256> {
                if (block == 6)
                {
                    return std::nullopt;
                }
                return history::test::blockHashOf(block);
            }));

    BOOST_CHECK(report.blockHashChecked);
    BOOST_REQUIRE_EQUAL(countFindings(report, HistoryFindingKind::BlockHashUnverifiable), 1);
    BOOST_CHECK_EQUAL(findingOf(report, HistoryFindingKind::BlockHashUnverifiable).block, 6);
    BOOST_CHECK_EQUAL(countFindings(report, HistoryFindingKind::BlockHashMismatch), 0);
}

/// POSITIVE — a normally expired store: the commit path's expiry advanced the boundary to the
/// block it dropped, in the same batch, so the boundary lands exactly one below the new oldest.
BOOST_AUTO_TEST_CASE(normallyExpiredStoreHasAConsistentBoundary)
{
    HistoryAuditFixture fixture;
    auto const expired = history::test::expireBlockAdvancing(fixture.store, fixture.storage, 5);
    BOOST_CHECK_EQUAL(expired.shardsDeleted, 1);

    auto const report = fixture.audit(/*depth=*/5);
    BOOST_CHECK(report.consistent());
    BOOST_CHECK_EQUAL(report.oldestRetained, 6);
    BOOST_REQUIRE(report.retentionBoundary.has_value());
    BOOST_CHECK_EQUAL(*report.retentionBoundary, 5);
    BOOST_CHECK_EQUAL(report.indexVersions, 10);
}

/// POSITIVE — a chain that started at genesis. seedRetentionBoundary clamps to 0, so the boundary
/// and the oldest block are BOTH 0; an unclamped "oldest - 1" would read this healthy store as
/// broken.
///
/// It is also where B.10 ③'s count would misfire if it were taken over every block on disk: the
/// rebuild starts at boundary + 1 = 1, so block 0's own record is legitimately outside the index —
/// it answers for block -1, which does not exist. `accountableRecords` excludes it and the audit
/// reports the store clean.
BOOST_AUTO_TEST_CASE(genesisSeededBoundaryEqualsTheOldestBlock)
{
    history::StateHistoryStore store;
    history::test::HistoryMemStorage storage;
    for (protocol::BlockNumber block = 0; block <= 2; ++block)
    {
        history::test::Diff diff;
        diff.change("alpha"sv, std::nullopt);
        history::test::putBlock(store, storage, block, diff);
    }
    bcos::task::syncWait(
        history::seedRetentionBoundary<history::StateHistoryStore>(storage, std::nullopt, 0));

    auto const report = bcos::task::syncWait(auditStateHistory(storage, 2, 8, 0, ledgerHashes()));
    BOOST_CHECK(report.consistent());
    BOOST_CHECK_EQUAL(report.oldestRetained, 0);
    BOOST_REQUIRE(report.retentionBoundary.has_value());
    BOOST_CHECK_EQUAL(*report.retentionBoundary, 0);
    BOOST_CHECK_EQUAL(report.declaredRecords, 3);
    BOOST_CHECK_EQUAL(report.accountableRecords, 2);
    BOOST_CHECK_EQUAL(report.indexVersions, 2);
}

/// A structurally undecodable row is not a finding, it is a scan that cannot run: the audit
/// cannot say what the block holds, so it throws rather than reporting a count it did not read.
BOOST_AUTO_TEST_CASE(undecodableMetaRowThrowsRatherThanReporting)
{
    HistoryAuditFixture fixture;
    history::test::overwriteRow(
        fixture.storage, history::kStateHistory.shard, history::metaRowKey(8), "short");

    BOOST_CHECK_THROW(static_cast<void>(fixture.audit()), MPTInvariantViolation);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::audit::test
