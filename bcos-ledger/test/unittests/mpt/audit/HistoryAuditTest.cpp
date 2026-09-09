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
 * @brief spec B.10's four checks, one injected corruption each
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
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <string>
#include <string_view>

using namespace std::string_view_literals;

namespace bcos::ledger::mpt::audit::test
{

BOOST_AUTO_TEST_SUITE(HistoryAuditSuite)

/// Six blocks of state history, 5..10, two keys each. The chain tip is 10 and the retention depth
/// is 6, so the window is exactly [5, 10] — every B.10 check has something to agree with.
///
/// Block 5 also seeds the retention boundary the way the commit path does — seedRetentionBoundary
/// with no prior value, which writes firstRecorded - 1 — so the store is shaped like one a running
/// node produced rather than like manifests with no metadata behind them.
struct HistoryAuditFixture
{
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
            history::test::putBlock(storage, block, diff);
        }
        bcos::task::syncWait(history::seedRetentionBoundary<history::StateHistoryStore>(
            storage, std::nullopt, kOldest));
    }

    [[nodiscard]] HistoryAuditReport audit(
        protocol::BlockNumber depth = kDepth, protocol::BlockNumber tip = kTip)
    {
        return bcos::task::syncWait(auditStateHistory(storage, tip, depth));
    }
};

/// How many findings of @p kind a report carries.
std::size_t countFindings(HistoryAuditReport const& report, HistoryFindingKind kind)
{
    return static_cast<std::size_t>(std::count_if(report.findings.begin(), report.findings.end(),
        [kind](HistoryFinding const& finding) { return finding.kind == kind; }));
}

/// Put a row into the index or manifest table by its raw row key — the shape a corruption has.
void putRawHistoryRow(history::test::HistoryMemStorage& storage, std::string_view table,
    std::string rowKey, std::string value)
{
    bcos::storage::Entry entry;
    entry.set(std::move(value));
    bcos::task::syncWait(
        bcos::storage2::writeOne(storage, executor_v1::StateKey{table, rowKey}, std::move(entry)));
}

/// Erase one row outright — a lost index row or a lost manifest shard.
void removeRawHistoryRow(
    history::test::HistoryMemStorage& storage, std::string_view table, std::string rowKey)
{
    bcos::task::syncWait(bcos::storage2::removeOne(storage, executor_v1::StateKey{table, rowKey}));
}

BOOST_AUTO_TEST_CASE(cleanWindowPasses)
{
    HistoryAuditFixture fixture;
    auto const report = fixture.audit();

    BOOST_CHECK(report.consistent());
    BOOST_CHECK_NO_THROW(report.throwIfInconsistent());
    BOOST_CHECK_EQUAL(report.windowStart, HistoryAuditFixture::kOldest);
    BOOST_CHECK_EQUAL(report.oldestRetained, HistoryAuditFixture::kOldest);
    BOOST_CHECK_EQUAL(report.newestRetained, HistoryAuditFixture::kTip);
    BOOST_CHECK_EQUAL(report.blocksWithManifest, 6);
    BOOST_CHECK_EQUAL(report.manifestKeys, 12);
    BOOST_CHECK_EQUAL(report.indexRows, 12);
    // boundary == oldest manifest - 1, the shape seedRetentionBoundary leaves behind.
    BOOST_REQUIRE(report.retentionBoundary.has_value());
    BOOST_CHECK_EQUAL(*report.retentionBoundary, HistoryAuditFixture::kOldest - 1);
}

/// The same generic component, second instantiation (spec B.8): the audit is written once and
/// must work for the trie-node history as well.
BOOST_AUTO_TEST_CASE(trieHistoryUsesTheSameAudit)
{
    history::test::HistoryMemStorage storage;
    for (protocol::BlockNumber block = 1; block <= 3; ++block)
    {
        history::test::Diff diff;
        diff.change("/mptp/a:\x00"sv, "node-before-" + std::to_string(block));
        bcos::task::syncWait(history::TrieHistoryStore::put(
            storage, block, diff.entries(), history::test::kWideShardCap));
    }

    bcos::task::syncWait(
        history::seedRetentionBoundary<history::TrieHistoryStore>(storage, std::nullopt, 1));

    auto const report = bcos::task::syncWait(auditTrieHistory(storage, 3, 3));
    BOOST_CHECK(report.consistent());
    BOOST_CHECK_EQUAL(report.blocksWithManifest, 3);
    BOOST_CHECK_EQUAL(report.indexRows, 3);
}

/// NEGATIVE CONTROL — B.10 ①, manifest -> index. The pre-image a block promised is gone.
BOOST_AUTO_TEST_CASE(deletedIndexRowIsReportedAsBOneManifestKeyWithoutIndexRow)
{
    HistoryAuditFixture fixture;
    auto const key = history::test::makeBytes("alpha"sv);
    removeRawHistoryRow(
        fixture.storage, history::kStateHistory.index, history::indexRowKey(key, 7));

    auto const report = fixture.audit();
    BOOST_CHECK(!report.consistent());
    BOOST_CHECK_EQUAL(countFindings(report, HistoryFindingKind::ManifestKeyWithoutIndexRow), 1);
    BOOST_REQUIRE(!report.findings.empty());
    BOOST_CHECK_EQUAL(report.findings.front().block, 7);
    BOOST_CHECK_THROW(report.throwIfInconsistent(), MPTInvariantViolation);
}

/// NEGATIVE CONTROL — B.10 ②, the fatal one: a block inside the window has no manifest, so every
/// block older than it is unreachable by a backwards walk.
BOOST_AUTO_TEST_CASE(missingManifestInsideTheWindowIsReportedAsBTwo)
{
    HistoryAuditFixture fixture;
    // Drop block 7's manifest AND its index rows, so nothing but the gap itself is reported.
    removeRawHistoryRow(
        fixture.storage, history::kStateHistory.manifest, history::manifestRowKey(7, 0));
    for (auto const& name : {"alpha"sv, "beta"sv})
    {
        removeRawHistoryRow(fixture.storage, history::kStateHistory.index,
            history::indexRowKey(history::test::makeBytes(name), 7));
    }

    auto const report = fixture.audit();
    BOOST_REQUIRE_EQUAL(report.findings.size(), 1);
    BOOST_CHECK(report.findings.front().kind == HistoryFindingKind::MissingManifest);
    BOOST_CHECK_EQUAL(report.findings.front().block, 7);
}

/// NEGATIVE CONTROL — B.10 ③: an index row pointing at a block whose manifest does not exist. In
/// B.5's ordering the manifest is deleted LAST precisely so this cannot happen; seeing it means
/// an expiry ran backwards or a shard was lost.
BOOST_AUTO_TEST_CASE(indexRowForABlockWithNoManifestIsReportedAsBThree)
{
    HistoryAuditFixture fixture;
    putRawHistoryRow(fixture.storage, history::kStateHistory.index,
        history::indexRowKey(history::test::makeBytes("alpha"sv), 3),
        history::indexRowValue(std::nullopt));

    auto const report = fixture.audit();
    BOOST_CHECK_EQUAL(countFindings(report, HistoryFindingKind::OrphanIndexRow), 1);
    BOOST_REQUIRE(!report.findings.empty());
    BOOST_CHECK_EQUAL(report.findings.front().block, 3);
}

/// NEGATIVE CONTROL — B.10 ①, index -> manifest. The row exists and its block does too, but the
/// manifest never listed the key, so expiry would leave the row behind forever.
BOOST_AUTO_TEST_CASE(indexRowNotListedByItsManifestIsReportedAsBOne)
{
    HistoryAuditFixture fixture;
    putRawHistoryRow(fixture.storage, history::kStateHistory.index,
        history::indexRowKey(history::test::makeBytes("gamma"sv), 8),
        history::indexRowValue(std::nullopt));

    auto const report = fixture.audit();
    BOOST_CHECK_EQUAL(countFindings(report, HistoryFindingKind::IndexRowNotInManifest), 1);
    BOOST_REQUIRE(!report.findings.empty());
    BOOST_CHECK_EQUAL(report.findings.front().block, 8);
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
    // Block 11 is inside the window and has no manifest, so ② fires for it too — the two checks
    // see the same damage from different angles and both are worth reporting.
    BOOST_CHECK_EQUAL(countFindings(report, HistoryFindingKind::MissingManifest), 1);
}

/// A node that turned history on part-way through its life has no manifests for the blocks before
/// that, and those are not holes.
BOOST_AUTO_TEST_CASE(firstHistoryBlockMovesTheWindowStart)
{
    HistoryAuditFixture fixture;
    auto const noisy = bcos::task::syncWait(auditStateHistory(fixture.storage, 10, 20));
    BOOST_CHECK_EQUAL(countFindings(noisy, HistoryFindingKind::MissingManifest), 5);

    auto const quiet = bcos::task::syncWait(auditStateHistory(fixture.storage, 10, 20, 5));
    BOOST_CHECK(quiet.consistent());
}

/// The boundary is what a SERVING query consults, so an audit that never reads it can call a
/// store healthy while every historical read below the oldest manifest answers with today's value.
/// The invariant in every regime is boundary == max(0, oldest manifest - 1).

/// NEGATIVE CONTROL — B.10 ④, (a): manifests with no boundary row behind them.
BOOST_AUTO_TEST_CASE(boundaryRowMissingWhileManifestsExistIsReportedAsBFour)
{
    HistoryAuditFixture fixture;
    removeRawHistoryRow(fixture.storage, history::kStateHistory.boundary,
        std::string(history::kRetentionBoundaryRowKey));

    auto const report = fixture.audit();
    BOOST_CHECK(!report.retentionBoundary.has_value());
    BOOST_REQUIRE_EQUAL(countFindings(report, HistoryFindingKind::RetentionBoundaryRowMismatch), 1);
    BOOST_CHECK(
        report.findings.front().detail.find("no retention-boundary row") != std::string::npos);
}

/// NEGATIVE CONTROL — B.10 ④, (b) in the DANGEROUS direction: the row claims heights whose
/// manifests are gone. A read at block 2 would seek past every row and answer HistoryUseCurrent —
/// today's value under block 2's number — because the window guard was told block 2 is covered.
BOOST_AUTO_TEST_CASE(boundaryBelowTheOldestManifestIsReportedAsBFour)
{
    HistoryAuditFixture fixture;
    bcos::task::syncWait(history::StateHistoryStore::writeRetentionBoundary(fixture.storage, 1));

    auto const report = fixture.audit();
    BOOST_REQUIRE_EQUAL(countFindings(report, HistoryFindingKind::RetentionBoundaryRowMismatch), 1);
    BOOST_CHECK_EQUAL(report.findings.front().block, 1);
    BOOST_CHECK(report.findings.front().detail.find("claims heights this store no longer holds") !=
                std::string::npos);
    BOOST_CHECK(report.findings.front().detail.find("TODAY's value") != std::string::npos);
}

/// NEGATIVE CONTROL — B.10 ④, (b) the other way: the row sits above the oldest manifest, so those
/// manifests are unreachable. Wasteful, not wrong, and the text says so.
BOOST_AUTO_TEST_CASE(boundaryAboveTheOldestManifestIsReportedAsBFour)
{
    HistoryAuditFixture fixture;
    bcos::task::syncWait(history::StateHistoryStore::writeRetentionBoundary(fixture.storage, 7));

    auto const report = fixture.audit();
    BOOST_REQUIRE_EQUAL(countFindings(report, HistoryFindingKind::RetentionBoundaryRowMismatch), 1);
    BOOST_CHECK_EQUAL(report.findings.front().block, 7);
    BOOST_CHECK(report.findings.front().detail.find("dead weight") != std::string::npos);
}

/// POSITIVE — a normally expired store: the commit path's expiry advanced the boundary to the
/// block it dropped, in the same batch, so the boundary lands exactly one below the new oldest.
BOOST_AUTO_TEST_CASE(normallyExpiredStoreHasAConsistentBoundary)
{
    HistoryAuditFixture fixture;
    auto const expired = history::test::expireBlockAdvancing(fixture.storage, 5);
    BOOST_CHECK_EQUAL(expired.manifestShardsDeleted, 1);

    auto const report = fixture.audit(/*depth=*/5);
    BOOST_CHECK(report.consistent());
    BOOST_CHECK_EQUAL(report.oldestRetained, 6);
    BOOST_REQUIRE(report.retentionBoundary.has_value());
    BOOST_CHECK_EQUAL(*report.retentionBoundary, 5);
}

/// POSITIVE — a chain that started at genesis. seedRetentionBoundary clamps to 0, so the boundary
/// and the oldest manifest are BOTH 0; an unclamped "oldest - 1" would read this healthy store as
/// broken.
BOOST_AUTO_TEST_CASE(genesisSeededBoundaryEqualsTheOldestManifest)
{
    history::test::HistoryMemStorage storage;
    for (protocol::BlockNumber block = 0; block <= 2; ++block)
    {
        history::test::Diff diff;
        diff.change("alpha"sv, std::nullopt);
        history::test::putBlock(storage, block, diff);
    }
    bcos::task::syncWait(
        history::seedRetentionBoundary<history::StateHistoryStore>(storage, std::nullopt, 0));

    auto const report = bcos::task::syncWait(auditStateHistory(storage, 2, 8));
    BOOST_CHECK(report.consistent());
    BOOST_CHECK_EQUAL(report.oldestRetained, 0);
    BOOST_REQUIRE(report.retentionBoundary.has_value());
    BOOST_CHECK_EQUAL(*report.retentionBoundary, 0);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::audit::test
