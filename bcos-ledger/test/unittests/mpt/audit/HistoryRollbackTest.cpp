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
 * @file HistoryRollbackTest.cpp
 * @brief Reverse-applying two blocks of both histories lands the live plane on the state it had
 *        at the target block (pathdb spec §11)
 */

#include "../history/HistoryTestHelpers.h"
#include "AuditTestHelpers.h"
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/mpt/Errors.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-ledger/mpt/audit/HistoryAudit.h>
#include <bcos-ledger/mpt/audit/HistoryRollback.h>
#include <bcos-ledger/mpt/audit/PathTreeAudit.h>
#include <bcos-ledger/mpt/history/HistoryCommit.h>
#include <bcos-ledger/mpt/history/HistoryErrors.h>
#include <bcos-ledger/mpt/history/HistoryRowCodec.h>
#include <bcos-ledger/mpt/history/ReverseHistoryStore.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-task/Wait.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <deque>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace std::string_view_literals;

namespace bcos::ledger::mpt::audit::test
{

BOOST_AUTO_TEST_SUITE(HistoryRollbackSuite)

/// A live row, addressed the way the history addresses it: by its PHYSICAL key, table and row key
/// joined by the colon StateKeyResolver splits on.
template <class Storage>
void writeLiveRow(Storage& storage, std::string_view physicalKey, std::string_view value)
{
    bcos::storage::Entry entry;
    entry.set(std::string(value));
    bcos::task::syncWait(bcos::storage2::writeOne(
        storage, executor_v1::StateKey{std::string(physicalKey)}, std::move(entry)));
}

template <class Storage>
std::optional<std::string> readLiveRow(Storage& storage, std::string_view physicalKey)
{
    return bcos::task::syncWait([&]() -> bcos::task::Task<std::optional<std::string>> {
        auto entry = co_await bcos::storage2::readOne(
            storage, executor_v1::StateKey{std::string(physicalKey)});
        if (!entry)
        {
            co_return std::nullopt;
        }
        co_return std::string(entry->get());
    }());
}

/// The physical key of the row Ledger keeps the chain tip in.
std::string currentNumberKey()
{
    return std::string(ledger::SYS_CURRENT_STATE) + ':' +
           std::string(ledger::SYS_KEY_CURRENT_NUMBER);
}

/// Whether block @p block still has a meta row in @p Tables — "is this block's history still
/// there", which is what the rollback deletes as it finishes with each block.
template <history::HistoryTables const& Tables, class Storage>
bool hasHistory(Storage& storage, protocol::BlockNumber block)
{
    return bcos::task::syncWait(bcos::storage2::readOne(storage, executor_v1::StateKey{Tables.shard,
                                                                     history::metaRowKey(block)}))
        .has_value();
}

/// Records block @p block's state history holds, read straight off its rows.
template <class Storage>
std::size_t stateRecordsOf(Storage& storage, protocol::BlockNumber block)
{
    history::StateHistoryStore store;
    return bcos::task::syncWait(store.readBlock(storage, block)).records.size();
}

/// Three committed blocks, both histories recorded, with the state plane standing at block 3.
///
///   key                              b1        b2        b3
///   /apps/a:x                        "v1"      "v2"      "v3"
///   /apps/a:y                        -         "w2"      deleted
///   s_current_state:current_number   "1"       "2"       "3"
///   /mptp/a:<root position>          "n1"      "n2"      "n3"
struct RollbackFixture
{
    history::StateHistoryStore stateStore;
    history::TrieHistoryStore trieStore;
    history::test::HistoryMemStorage storage;
    static constexpr protocol::BlockNumber kTip = 3;
    static constexpr protocol::BlockNumber kDepth = 8;
    std::string const nodeKey = std::string("/mptp/a:") + '\x00';

    RollbackFixture()
    {
        commitBlock(1, "v1", std::nullopt, std::nullopt, std::nullopt, "n1", std::nullopt);
        commitBlock(2, "v2", "v1"sv, "w2"sv, std::nullopt, "n2", "n1"sv);
        commitBlock(3, "v3", "v2"sv, std::nullopt, "w2"sv, "n3", "n2"sv);
    }

    /// Apply one block to the live plane AND record what each touched key held before it — the
    /// pairing the commit path is responsible for (spec §9).
    void commitBlock(protocol::BlockNumber block, std::string_view xValue,
        std::optional<std::string_view> xOld, std::optional<std::string_view> yValue,
        std::optional<std::string_view> yOld, std::string_view nodeValue,
        std::optional<std::string_view> nodeOld)
    {
        std::string const previousNumber = std::to_string(block - 1);
        history::test::Diff stateDiff;
        stateDiff.change("/apps/a:x"sv, xOld);
        stateDiff.change("/apps/a:y"sv, yOld);
        // NOTE: the tip row is NOT in the real capture set — HistoryCommit.h's
        // isHistoricalStateRow keeps only /apps/ nonce, balance, codeHash and slot rows, and
        // collectStateHistoryKeys runs before Ledger writes the tip anyway. It is recorded here on
        // purpose, to simulate a wider capture set than the implementation records, so that the
        // "reverse-apply puts back whatever the history holds" mechanics can be asserted on a row
        // whose value is easy to read. Nothing downstream may conclude from this fixture that a
        // real rollback restores the tip.
        stateDiff.change(currentNumberKey(), block == 1 ?
                                                 std::optional<std::string_view>{} :
                                                 std::optional<std::string_view>{previousNumber});
        history::test::putBlock(stateStore, storage, block, stateDiff);

        history::test::Diff trieDiff;
        trieDiff.change(nodeKey, nodeOld);
        history::test::putBlock(trieStore, storage, block, trieDiff);

        writeLiveRow(storage, "/apps/a:x", xValue);
        if (yValue)
        {
            writeLiveRow(storage, "/apps/a:y", *yValue);
        }
        else if (block > 1)
        {
            bcos::task::syncWait(bcos::storage2::removeOne(
                storage, executor_v1::StateKey{std::string("/apps/a:y")}));
        }
        writeLiveRow(storage, currentNumberKey(), std::to_string(block));
        writeLiveRow(storage, nodeKey, nodeValue);
    }

    RollbackReport rollback(protocol::BlockNumber target, bool apply)
    {
        return bcos::task::syncWait(rollbackTo(storage, kTip, target, kDepth, kDepth, apply));
    }
};

BOOST_AUTO_TEST_CASE(dryRunCountsWithoutTouchingAnything)
{
    RollbackFixture fixture;
    auto const report = fixture.rollback(1, /*apply=*/false);

    BOOST_CHECK(!report.applied);
    BOOST_CHECK_EQUAL(report.blocks, 2);
    // Three state keys and one trie key per block, two blocks — counted off the meta rows.
    BOOST_CHECK_EQUAL(report.stateRows, 6);
    BOOST_CHECK_EQUAL(report.trieRows, 2);
    BOOST_CHECK_EQUAL(report.rowsWritten, 0);
    BOOST_CHECK_EQUAL(report.rowsDeleted, 0);

    // Nothing moved.
    BOOST_CHECK_EQUAL(*readLiveRow(fixture.storage, "/apps/a:x"), "v3");
    BOOST_CHECK_EQUAL(stateRecordsOf(fixture.storage, 3), 3);
}

BOOST_AUTO_TEST_CASE(rollbackRestoresBothPlanesToTheTargetBlock)
{
    RollbackFixture fixture;
    auto const report = fixture.rollback(1, /*apply=*/true);

    BOOST_CHECK(report.applied);
    BOOST_CHECK_EQUAL(report.blocks, 2);
    BOOST_CHECK_EQUAL(report.rowsWritten + report.rowsDeleted, 8);

    // State plane back at the end of block 1.
    BOOST_CHECK_EQUAL(*readLiveRow(fixture.storage, "/apps/a:x"), "v1");
    BOOST_CHECK(!readLiveRow(fixture.storage, "/apps/a:y").has_value());
    // Trie plane too: a node row is an ordinary state row, so one code path serves both.
    BOOST_CHECK_EQUAL(*readLiveRow(fixture.storage, fixture.nodeKey), "n1");

    // The tip row is restored BY the state history, not written by the tool.
    BOOST_REQUIRE(report.currentNumberRow.has_value());
    BOOST_CHECK_EQUAL(*report.currentNumberRow, "1");

    // The rolled-back blocks' history is gone; the target block's is not.
    BOOST_CHECK(!hasHistory<history::kStateHistory>(fixture.storage, 3));
    BOOST_CHECK(!hasHistory<history::kTrieHistory>(fixture.storage, 2));
    BOOST_CHECK_EQUAL(stateRecordsOf(fixture.storage, 1), 3);
}

/// G4's delete asymmetry, on the row the rollback is allowed to remove and the ones it is not:
/// `/apps/a:y` did not exist before block 2, so block 2's record for it is ABSENT and the rollback
/// deletes it. Every other key had a value, and each of those is WRITTEN BACK — never removed,
/// whatever the live plane holds.
BOOST_AUTO_TEST_CASE(onlyAbsentRecordsBecomeDeletes)
{
    RollbackFixture fixture;
    auto const report = fixture.rollback(1, /*apply=*/true);

    // Block 3: x, y and the tip row all had values (3 writes) plus one node row (1 write).
    // Block 2: x and the tip row had values (2 writes), y did not exist (1 delete), plus the node
    // row (1 write).
    BOOST_CHECK_EQUAL(report.rowsDeleted, 1);
    BOOST_CHECK_EQUAL(report.rowsWritten, 7);
    BOOST_CHECK(!readLiveRow(fixture.storage, "/apps/a:y").has_value());
}

BOOST_AUTO_TEST_CASE(rollingBackOneBlockStopsAtTheBlockBelow)
{
    RollbackFixture fixture;
    fixture.rollback(2, /*apply=*/true);

    BOOST_CHECK_EQUAL(*readLiveRow(fixture.storage, "/apps/a:x"), "v2");
    BOOST_CHECK_EQUAL(*readLiveRow(fixture.storage, "/apps/a:y"), "w2");
    BOOST_CHECK_EQUAL(*readLiveRow(fixture.storage, fixture.nodeKey), "n2");
}

/// The rollback discards from the TOP, so the oldest block the store can answer for does not move
/// — RetentionBoundary::Keep. Advancing it would refuse the walk's own next step.
BOOST_AUTO_TEST_CASE(rollbackLeavesTheRetentionBoundaryWhereItWas)
{
    RollbackFixture fixture;
    bcos::task::syncWait(history::seedRetentionBoundary<history::StateHistoryStore>(
        fixture.storage, std::nullopt, 1));
    bcos::task::syncWait(history::seedRetentionBoundary<history::TrieHistoryStore>(
        fixture.storage, std::nullopt, 1));

    fixture.rollback(1, /*apply=*/true);

    auto const stateBoundary =
        bcos::task::syncWait(history::StateHistoryStore::retentionBoundary(fixture.storage));
    auto const trieBoundary =
        bcos::task::syncWait(history::TrieHistoryStore::retentionBoundary(fixture.storage));
    BOOST_REQUIRE(stateBoundary.has_value());
    BOOST_REQUIRE(trieBoundary.has_value());
    BOOST_CHECK_EQUAL(*stateBoundary, 0);
    BOOST_CHECK_EQUAL(*trieBoundary, 0);
}

BOOST_AUTO_TEST_CASE(targetOutsideTheRetentionWindowIsRefusedBeforeAnyWrite)
{
    RollbackFixture fixture;
    // depth 1 means only block 3's pre-images are retained, so block 1 is unreachable.
    BOOST_CHECK_THROW(bcos::task::syncWait(rollbackTo(
                          fixture.storage, RollbackFixture::kTip, 1, 1, 1, /*apply=*/true)),
        history::HistoryPruned);
    BOOST_CHECK_EQUAL(*readLiveRow(fixture.storage, "/apps/a:x"), "v3");
}

/// NEGATIVE CONTROL — a node that never recorded one of the two histories. The refusal is right
/// either way; what this pins is the DIAGNOSIS. "target 1 is older than the retained window (tip
/// 3, depth 0)" is true and useless: it reads as "raise the target", and no target works.
BOOST_AUTO_TEST_CASE(depthZeroIsRefusedAsNeverRecordedNotAsOutOfWindow)
{
    RollbackFixture fixture;
    BOOST_CHECK_EXCEPTION(bcos::task::syncWait(rollbackTo(
                              fixture.storage, RollbackFixture::kTip, 1, 0, 8, /*apply=*/true)),
        history::InvalidHistoryBlock, [](history::InvalidHistoryBlock const& error) {
            std::string const message = boost::diagnostic_information(error);
            BOOST_TEST_MESSAGE("depth-0 refusal: " << message);
            return message.find("retains no StateHistory (depth 0)") != std::string::npos &&
                   message.find("state plane cannot be rolled back") != std::string::npos;
        });
    BOOST_CHECK_EXCEPTION(bcos::task::syncWait(rollbackTo(
                              fixture.storage, RollbackFixture::kTip, 1, 8, 0, /*apply=*/true)),
        history::InvalidHistoryBlock, [](history::InvalidHistoryBlock const& error) {
            std::string const message = boost::diagnostic_information(error);
            return message.find("retains no TrieHistory (depth 0)") != std::string::npos &&
                   message.find("trie node rows cannot be rolled back") != std::string::npos;
        });

    // Refused before anything moved.
    BOOST_CHECK_EQUAL(*readLiveRow(fixture.storage, "/apps/a:x"), "v3");
}

BOOST_AUTO_TEST_CASE(targetAtOrAboveTheTipIsRejected)
{
    RollbackFixture fixture;
    BOOST_CHECK_THROW(fixture.rollback(3, /*apply=*/true), history::InvalidHistoryBlock);
    BOOST_CHECK_THROW(fixture.rollback(-1, /*apply=*/true), history::InvalidHistoryBlock);
}

/// The pairing the CLI performs after `rollback --yes`: put the trie plane back, then prove the
/// tree it landed on is the one the TARGET block's header commits to.
///
/// Everything here is built by the real trie builder — the block-2 pre-images are
/// PathMergeResult::preimages, the same field the commit path feeds TrieHistory — so the rollback
/// is undoing a genuine trie rebuild rather than hand-written rows.
BOOST_AUTO_TEST_CASE(rollbackLandsOnATreeThatVerifiesAgainstTheTargetRoot)
{
    mpt::test::NodeMemoryStorage nodes;
    history::test::HistoryMemStorage flat;
    history::StateHistoryStore stateStore;
    history::TrieHistoryStore trieStore;

    // Block 1: two accounts.
    std::map<bcos::h256, bcos::bytes> first;
    first[accountKeyHash(mpt::test::makeAddress(0x01))] = Account{}.encode();
    first[accountKeyHash(mpt::test::makeAddress(0x02))] = Account{}.encode();
    auto const rootAtOne = mpt::test::seedTrieFlushed(nodes, emptyRootHash(), first).root;
    landNodeRowsOnFlat(nodes, flat);
    writeLiveRow(flat, currentNumberKey(), "1");
    history::test::Diff blockOne;
    blockOne.change(currentNumberKey(), std::nullopt);
    history::test::putBlock(stateStore, flat, 1, blockOne);
    history::test::Diff const emptyTrie;
    history::test::putBlock(trieStore, flat, 1, emptyTrie);

    // Block 2: a third account, committed over block 1's root.
    auto const rebuilt = mpt::test::commitTrieFlushed(nodes, rootAtOne,
        {{accountKeyHash(mpt::test::makeAddress(0x03)),
            std::optional<bcos::bytes>{Account{}.encode()}}});
    auto const rootAtTwo = rebuilt.root;
    BOOST_REQUIRE_NE(rootAtOne.hex(), rootAtTwo.hex());
    for (auto const& [key, raw] : rebuilt.upserts)
    {
        writeNodeRow(flat, key, raw);
    }
    for (auto const& key : rebuilt.deletes)
    {
        removeNodeRow(flat, key);
    }

    // The block-2 trie history: every touched position and what it held before.
    std::deque<bcos::bytes> owned;
    std::vector<history::HistoryEntry> entries;
    for (auto const& [key, prior] : rebuilt.preimages)
    {
        auto const rowKey = pathNodeStateKey(key);
        auto const& keyBytes = owned.emplace_back(
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            reinterpret_cast<bcos::byte const*>(rowKey.data()),
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            reinterpret_cast<bcos::byte const*>(rowKey.data()) + rowKey.size());
        std::optional<std::span<const bcos::byte>> oldValue;
        if (prior)
        {
            oldValue = std::span<const bcos::byte>(owned.emplace_back(*prior));
        }
        entries.push_back(history::HistoryEntry{.key = keyBytes, .oldValue = oldValue});
    }
    trieStore.publish(bcos::task::syncWait(trieStore.put(flat, 2, history::test::blockHashOf(2),
                          entries, history::test::kWideShardCap)),
        std::nullopt, std::nullopt);

    history::test::Diff blockTwo;
    blockTwo.change(currentNumberKey(), "1"sv);
    history::test::putBlock(stateStore, flat, 2, blockTwo);
    writeLiveRow(flat, currentNumberKey(), "2");

    // The store stands at block 2 and verifies against block 2's root, not block 1's.
    BOOST_CHECK_NO_THROW(runPathTreeAudit(flat, rootAtTwo));
    BOOST_CHECK_THROW(runPathTreeAudit(flat, rootAtOne), MPTInvariantViolation);

    auto const report = bcos::task::syncWait(rollbackTo(flat, 2, 1, 8, 8, /*apply=*/true));
    BOOST_CHECK_EQUAL(report.blocks, 1);
    BOOST_REQUIRE(report.currentNumberRow.has_value());
    BOOST_CHECK_EQUAL(*report.currentNumberRow, "1");

    // NEGATIVE CONTROL for the post-rollback check: the tree is internally consistent EITHER WAY,
    // so only the comparison against the target block's root says which height it stands at.
    BOOST_CHECK_NO_THROW(runPathTreeAudit(flat));
    BOOST_CHECK_NO_THROW(runPathTreeAudit(flat, rootAtOne));
    BOOST_CHECK_THROW(runPathTreeAudit(flat, rootAtTwo), MPTInvariantViolation);
}

/// NEGATIVE CONTROL — a block inside the range whose history is gone must abort the WHOLE
/// rollback before a single row moves.
///
/// The coverage contract (HistoryTables.h) makes "no meta row" mean "these pre-images were never
/// captured, or have been expired", which is indistinguishable from a block that changed nothing
/// unless the meta row is checked: a block that changed nothing still HAS one. Without the
/// pre-check block 2 would look like "nothing to undo", block 3 alone would be reverse-applied,
/// and the live plane would be left straddling two block heights and reported as a success.
BOOST_AUTO_TEST_CASE(missingHistoryInsideTheRangeRefusesTheWholeRollback)
{
    RollbackFixture fixture;
    bcos::task::syncWait(fixture.stateStore.expire(fixture.storage, fixture.storage, 2));

    BOOST_CHECK_EXCEPTION(fixture.rollback(1, /*apply=*/true), MPTInvariantViolation,
        [](MPTInvariantViolation const& error) {
            std::string const message = boost::diagnostic_information(error);
            BOOST_TEST_MESSAGE("rollback refused: " << message);
            return message.find("block 2 has no StateHistory meta row") != std::string::npos &&
                   message.find("Nothing was written") != std::string::npos;
        });

    // Zero rows written: block 3 was not touched even though its own history is intact.
    BOOST_CHECK_EQUAL(*readLiveRow(fixture.storage, "/apps/a:x"), "v3");
    BOOST_CHECK(!readLiveRow(fixture.storage, "/apps/a:y").has_value());
    BOOST_CHECK_EQUAL(*readLiveRow(fixture.storage, fixture.nodeKey), "n3");
    BOOST_CHECK_EQUAL(*readLiveRow(fixture.storage, currentNumberKey()), "3");
    BOOST_CHECK_EQUAL(stateRecordsOf(fixture.storage, 3), 3);
}

/// The dry run refuses too — that is where an operator looks first, so it is where the refusal has
/// to land. Also the TrieHistory side of the same check: either store missing is enough.
BOOST_AUTO_TEST_CASE(missingHistoryInsideTheRangeAlsoRefusesTheDryRun)
{
    RollbackFixture fixture;
    bcos::task::syncWait(fixture.trieStore.expire(fixture.storage, fixture.storage, 2));

    BOOST_CHECK_EXCEPTION(fixture.rollback(1, /*apply=*/false), MPTInvariantViolation,
        [](MPTInvariantViolation const& error) {
            return std::string(boost::diagnostic_information(error))
                       .find("block 2 has no TrieHistory meta row") != std::string::npos;
        });
}

/// NEGATIVE CONTROL — an interrupted `--yes` run, which is NOT the same fault as a hole and must
/// not get the same message.
///
/// The walk drops each block's history as it undoes that block, and never moves the tip row, so a
/// run that stops part-way leaves a contiguous run of history-less blocks at the TOP of the range.
/// Re-running the same command then asks for pre-images that were consumed on purpose. "Check the
/// retention depths" is a dead end there; the refusal has to say what happened and give the tip to
/// re-run with.
BOOST_AUTO_TEST_CASE(interruptedRollbackIsDiagnosedAndNamesTheTipToResumeFrom)
{
    RollbackFixture fixture;
    // What the walk itself does to blocks 3 and 2 before being killed: both planes undone, both
    // stores' history for those blocks dropped, tip row untouched.
    for (protocol::BlockNumber block : {3, 2})
    {
        bcos::task::syncWait(fixture.stateStore.expire(fixture.storage, fixture.storage, block));
        bcos::task::syncWait(fixture.trieStore.expire(fixture.storage, fixture.storage, block));
    }

    BOOST_CHECK_EXCEPTION(fixture.rollback(0, /*apply=*/true), MPTInvariantViolation,
        [](MPTInvariantViolation const& error) {
            std::string const message = boost::diagnostic_information(error);
            BOOST_TEST_MESSAGE("interrupted-rollback refusal: " << message);
            return message.find("blocks 2..3 have no history") != std::string::npos &&
                   message.find("previous rollback was probably interrupted") !=
                       std::string::npos &&
                   message.find("Re-run with --tip 1") != std::string::npos &&
                   message.find("Nothing was written") != std::string::npos &&
                   // The residue the re-run does not reach: the two stores' expiries are separate
                   // writes, so an interruption between them leaves one block's trie rows behind.
                   message.find("BOTH stores report the same retained span") != std::string::npos;
        });
    // Not the generic diagnosis, which would send the operator to the retention depths.
    BOOST_CHECK_EXCEPTION(fixture.rollback(0, /*apply=*/true), MPTInvariantViolation,
        [](MPTInvariantViolation const& error) {
            return std::string(boost::diagnostic_information(error))
                       .find("Check the retention depths") == std::string::npos;
        });
    // Block 1's own history is intact, so the resumed run has something to do.
    BOOST_CHECK(hasHistory<history::kStateHistory>(fixture.storage, 1));
    BOOST_CHECK_EQUAL(*readLiveRow(fixture.storage, currentNumberKey()), "3");
}

/// The degenerate end of the same shape: NOTHING in the range has history. Naming a `--tip` here
/// would name the target itself, which is not a rollback — so the refusal says what the two
/// possible causes are instead.
BOOST_AUTO_TEST_CASE(anEmptyRangeSaysTheRollbackMayAlreadyBeDone)
{
    RollbackFixture fixture;
    for (protocol::BlockNumber block : {3, 2})
    {
        bcos::task::syncWait(fixture.stateStore.expire(fixture.storage, fixture.storage, block));
        bcos::task::syncWait(fixture.trieStore.expire(fixture.storage, fixture.storage, block));
    }

    BOOST_CHECK_EXCEPTION(fixture.rollback(1, /*apply=*/true), MPTInvariantViolation,
        [](MPTInvariantViolation const& error) {
            std::string const message = boost::diagnostic_information(error);
            BOOST_TEST_MESSAGE("empty-range refusal: " << message);
            return message.find("nothing left to undo") != std::string::npos &&
                   message.find("only the tip row is left to set") != std::string::npos &&
                   message.find("same retained span") != std::string::npos &&
                   message.find("--tip") == std::string::npos;
        });
}

/// A block that genuinely changed nothing is NOT a missing block: put() always writes a meta row
/// and an empty shard 0, so the block is present with a record count of zero and the rollback
/// walks straight past it.
BOOST_AUTO_TEST_CASE(blockThatChangedNothingDoesNotLookLikeAHole)
{
    history::StateHistoryStore stateStore;
    history::TrieHistoryStore trieStore;
    history::test::HistoryMemStorage storage;
    history::test::Diff first;
    first.change("/apps/a:x"sv, std::nullopt);
    history::test::putBlock(stateStore, storage, 1, first);
    history::test::putBlock(trieStore, storage, 1, first);

    history::test::Diff const empty;
    for (protocol::BlockNumber block = 2; block <= 3; ++block)
    {
        history::test::putBlock(stateStore, storage, block, empty);
        history::test::putBlock(trieStore, storage, block, empty);
    }
    writeLiveRow(storage, "/apps/a:x", "v1");

    auto const report = bcos::task::syncWait(rollbackTo(storage, 3, 1, 8, 8, /*apply=*/true));
    BOOST_CHECK_EQUAL(report.blocks, 2);
    BOOST_CHECK_EQUAL(report.rowsWritten + report.rowsDeleted, 0);
    BOOST_CHECK_EQUAL(*readLiveRow(storage, "/apps/a:x"), "v1");
}

/// NEGATIVE CONTROL — a block whose meta row survives while a shard it declares does not. The
/// diff on offer is SHORT, and applying a short diff would leave the rows it lost standing at
/// their post-block values with nothing reporting it, so the rollback stops (G6).
BOOST_AUTO_TEST_CASE(shortBlockDiffStopsTheRollback)
{
    RollbackFixture fixture;
    history::test::deleteRow(
        fixture.storage, history::kStateHistory.shard, history::shardRowKey(3, 0));

    BOOST_CHECK_THROW(fixture.rollback(1, /*apply=*/true), MPTInvariantViolation);
    // Block 3's own live rows are untouched: readBlock throws before a single write goes out.
    BOOST_CHECK_EQUAL(*readLiveRow(fixture.storage, "/apps/a:x"), "v3");
}

/// The CLI's `--yes` leg, end to end, against the storage production runs on.
///
/// Two real trie versions, block 2's PathMergeResult::preimages fed to TrieHistory, an /apps/ row
/// in the state history shaped the way isHistoricalStateRow captures them, headers and
/// number->hash rows for both blocks and a seeded retention boundary — a store that looks like a
/// node's, not like a test's. One copy is rolled back here with assertions; further identical
/// copies are left on disk under MPT_AUDIT_TEST_DB_DIR so the `mpt-audit` smoke has pristine
/// stores to run against.
struct RocksDbRollbackStore
{
    bcos::h256 rootAtOne;
    bcos::h256 rootAtTwo;
    /// The /apps/ row block 2 created — deleted again by a rollback to block 1.
    std::string newAccountRow;
};

RocksDbRollbackStore buildRocksDbRollbackStore(RocksDbStateStorage& storage)
{
    mpt::test::NodeMemoryStorage nodes;
    RocksDbRollbackStore built;
    history::StateHistoryStore stateStore;
    history::TrieHistoryStore trieStore;

    std::map<bcos::h256, bcos::bytes> first;
    first[accountKeyHash(mpt::test::makeAddress(0x01))] = Account{}.encode();
    first[accountKeyHash(mpt::test::makeAddress(0x02))] = Account{}.encode();
    auto const seeded = mpt::test::seedTrieFlushed(nodes, emptyRootHash(), first);
    built.rootAtOne = seeded.root;
    landNodeRowsOnFlat(nodes, storage);

    // Block 1's histories. The state side records the two accounts' nonce rows, which is the shape
    // isHistoricalStateRow admits; both were absent before block 1.
    history::test::Diff blockOneState;
    blockOneState.change("/apps/0100000000000000000000000000000000000000:nonce"sv, std::nullopt);
    blockOneState.change("/apps/0200000000000000000000000000000000000000:nonce"sv, std::nullopt);
    bcos::task::syncWait(stateStore.put(storage, 1, history::test::blockHashOf(1),
        blockOneState.entries(), history::kHistoryShardByteCap));
    bcos::task::syncWait(trieStore.put(
        storage, 1, history::test::blockHashOf(1), {}, history::kHistoryShardByteCap));
    bcos::task::syncWait(
        history::seedRetentionBoundary<history::StateHistoryStore>(storage, std::nullopt, 1));
    bcos::task::syncWait(
        history::seedRetentionBoundary<history::TrieHistoryStore>(storage, std::nullopt, 1));
    writeLiveRow(storage, "/apps/0100000000000000000000000000000000000000:nonce", "1");
    writeLiveRow(storage, "/apps/0200000000000000000000000000000000000000:nonce", "1");

    // Block 2: a third account.
    auto const rebuilt = mpt::test::commitTrieFlushed(nodes, built.rootAtOne,
        {{accountKeyHash(mpt::test::makeAddress(0x03)),
            std::optional<bcos::bytes>{Account{}.encode()}}});
    built.rootAtTwo = rebuilt.root;
    for (auto const& [key, raw] : rebuilt.upserts)
    {
        writeNodeRow(storage, key, raw);
    }
    for (auto const& key : rebuilt.deletes)
    {
        removeNodeRow(storage, key);
    }

    std::deque<bcos::bytes> owned;
    std::vector<history::HistoryEntry> trieEntries;
    for (auto const& [key, prior] : rebuilt.preimages)
    {
        auto const rowKey = pathNodeStateKey(key);
        auto const& keyBytes = owned.emplace_back(
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            reinterpret_cast<bcos::byte const*>(rowKey.data()),
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            reinterpret_cast<bcos::byte const*>(rowKey.data()) + rowKey.size());
        std::optional<std::span<const bcos::byte>> oldValue;
        if (prior)
        {
            oldValue = std::span<const bcos::byte>(owned.emplace_back(*prior));
        }
        trieEntries.push_back(history::HistoryEntry{.key = keyBytes, .oldValue = oldValue});
    }
    bcos::task::syncWait(trieStore.put(
        storage, 2, history::test::blockHashOf(2), trieEntries, history::kHistoryShardByteCap));

    built.newAccountRow = "/apps/0300000000000000000000000000000000000000:nonce";
    history::test::Diff blockTwoState;
    blockTwoState.change(built.newAccountRow, std::nullopt);
    bcos::task::syncWait(stateStore.put(storage, 2, history::test::blockHashOf(2),
        blockTwoState.entries(), history::kHistoryShardByteCap));
    writeLiveRow(storage, built.newAccountRow, "1");

    // The chain metadata the CLI reads: a header per block, the number->hash row B.10 ⑤ compares
    // the meta rows against, and the tip.
    for (auto const& [block, root] : {std::pair{protocol::BlockNumber{1}, built.rootAtOne},
             std::pair{protocol::BlockNumber{2}, built.rootAtTwo}})
    {
        bcostars::protocol::BlockHeaderImpl header;
        header.setNumber(block);
        header.setStateRoot(root);
        bcos::bytes encoded;
        header.encode(encoded);
        writeLiveRow(storage,
            std::string(ledger::SYS_NUMBER_2_BLOCK_HEADER) + ':' + std::to_string(block),
            std::string(encoded.begin(), encoded.end()));

        auto const blockHash = history::test::blockHashOf(block);
        writeLiveRow(storage, std::string(ledger::SYS_NUMBER_2_HASH) + ':' + std::to_string(block),
            std::string(blockHash.begin(), blockHash.end()));
    }
    writeLiveRow(storage, currentNumberKey(), "2");
    return built;
}

/// The hash source the unit assertions use — the same rows the CLI reads.
std::optional<bcos::h256> ledgerHashOf(RocksDbStateStorage& storage, protocol::BlockNumber block)
{
    auto entry = bcos::task::syncWait(bcos::storage2::readOne(
        storage, executor_v1::StateKey{ledger::SYS_NUMBER_2_HASH, std::to_string(block)}));
    if (!entry)
    {
        return std::nullopt;
    }
    auto const raw = entry->get();
    if (raw.size() != bcos::h256::SIZE)
    {
        return std::nullopt;
    }
    return bcos::h256(bcos::bytesConstRef(
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        reinterpret_cast<bcos::byte const*>(raw.data()), raw.size()));
}

BOOST_AUTO_TEST_CASE(rocksDbRollbackAppliesAndLandsOnTheTargetRoot)
{
    auto const [base, keep] = auditRocksDbBase();

    // The copy the CLI smoke runs against: built, left standing at block 2, never rolled back.
    if (keep)
    {
        {
            AuditRocksDb kept(base + "-rollback", true);
            RocksDbStateStorage storage(*kept.db, bcos::storage2::rocksdb::StateKeyResolver{},
                bcos::storage2::rocksdb::StateValueResolver{});
            buildRocksDbRollbackStore(storage);
        }
        {
            // The same store with block 1's state history expired under the Keep policy — an
            // expiry that dropped rows without advancing the boundary. Oldest meta row 2, boundary
            // still 0, so the store claims to answer for block 1 with nothing left to answer from:
            // the direction of B.10 ④ that produces silently wrong historical reads, and the one
            // the CLI has to print the `detail` text for.
            AuditRocksDb badBoundary(base + "-badboundary", true);
            RocksDbStateStorage storage(*badBoundary.db,
                bcos::storage2::rocksdb::StateKeyResolver{},
                bcos::storage2::rocksdb::StateValueResolver{});
            buildRocksDbRollbackStore(storage);
            history::StateHistoryStore expiring;
            bcos::task::syncWait(expiring.expire(storage, storage, 1));

            auto const report = bcos::task::syncWait(auditStateHistory(storage, 2, 8, 1,
                [&storage](protocol::BlockNumber block) { return ledgerHashOf(storage, block); }));
            BOOST_CHECK_EQUAL(report.oldestRetained, 2);
            BOOST_REQUIRE(report.retentionBoundary.has_value());
            BOOST_CHECK_EQUAL(*report.retentionBoundary, 0);
            BOOST_REQUIRE(!report.findings.empty());
            BOOST_CHECK(report.findings.back().detail.find(
                            "claims heights this store no longer holds") != std::string::npos);
        }
        {
            // The same store with block 2's state META row gone while its shard survives — the
            // shape that makes a rebuild REFUSE. Nothing else in the kept set produces a B.10 (3)
            // finding, so without this the CLI's rendering of one is never exercised.
            AuditRocksDb noRebuild(base + "-norebuild", true);
            RocksDbStateStorage storage(*noRebuild.db, bcos::storage2::rocksdb::StateKeyResolver{},
                bcos::storage2::rocksdb::StateValueResolver{});
            buildRocksDbRollbackStore(storage);
            bcos::task::syncWait(bcos::storage2::removeOne(storage,
                bcos::executor_v1::StateKey{history::kStateHistory.shard, history::metaRowKey(2)}));

            auto const report = bcos::task::syncWait(auditStateHistory(storage, 2, 8, 1,
                [&storage](protocol::BlockNumber block) { return ledgerHashOf(storage, block); }));
            BOOST_CHECK(!report.rebuilt);
            auto const rejected = std::find_if(
                report.findings.begin(), report.findings.end(), [](HistoryFinding const& finding) {
                    return finding.kind == HistoryFindingKind::RebuildRejected;
                });
            BOOST_REQUIRE(rejected != report.findings.end());
            BOOST_CHECK_EQUAL(rejected->block, 2);
            // One line, no error-info dump: the block is the finding's, not the text's.
            BOOST_CHECK(rejected->detail.find("tag_historyBlock") == std::string::npos);
            BOOST_CHECK_EQUAL(
                std::count(rejected->detail.begin(), rejected->detail.end(), '\n'), 0);
        }
        {
            // The same store without block 1's header. A rollback to 1 succeeds in writing the
            // rows and then has nothing to check them against — the case the CLI must not report
            // as a clean run, since it has already modified the store.
            AuditRocksDb noHeader(base + "-noheader", true);
            RocksDbStateStorage storage(*noHeader.db, bcos::storage2::rocksdb::StateKeyResolver{},
                bcos::storage2::rocksdb::StateValueResolver{});
            buildRocksDbRollbackStore(storage);
            bcos::task::syncWait(bcos::storage2::removeOne(
                storage, bcos::executor_v1::StateKey{ledger::SYS_NUMBER_2_BLOCK_HEADER, "1"}));
        }
    }

    AuditRocksDb database(base + "-rollback-applied", false);
    RocksDbStateStorage storage(*database.db, bcos::storage2::rocksdb::StateKeyResolver{},
        bcos::storage2::rocksdb::StateValueResolver{});
    auto const built = buildRocksDbRollbackStore(storage);
    auto const hashes = [&storage](
                            protocol::BlockNumber block) { return ledgerHashOf(storage, block); };

    // Standing at block 2, and both histories audit clean against their own metadata and the
    // chain's hashes.
    BOOST_CHECK_NO_THROW(runPathTreeAudit(storage, built.rootAtTwo));
    BOOST_CHECK(bcos::task::syncWait(auditStateHistory(storage, 2, 8, 1, hashes)).consistent());
    BOOST_CHECK(bcos::task::syncWait(auditTrieHistory(storage, 2, 8, 1, hashes)).consistent());

    auto const report = bcos::task::syncWait(rollbackTo(storage, 2, 1, 8, 8, /*apply=*/true));
    BOOST_CHECK(report.applied);
    BOOST_CHECK_EQUAL(report.blocks, 1);

    // The trie plane landed on block 1's committed root, and block 2's account row is gone.
    BOOST_CHECK_NO_THROW(runPathTreeAudit(storage, built.rootAtOne));
    BOOST_CHECK_THROW(runPathTreeAudit(storage, built.rootAtTwo), MPTInvariantViolation);
    BOOST_CHECK(!readLiveRow(storage, built.newAccountRow).has_value());
    BOOST_CHECK_EQUAL(
        *readLiveRow(storage, "/apps/0100000000000000000000000000000000000000:nonce"), "1");

    // The tip row is NOT restored, and that is the decided behaviour, not a defect: it is outside
    // isHistoricalStateRow's capture set, so it still reads the pre-rollback tip.
    BOOST_REQUIRE(report.currentNumberRow.has_value());
    BOOST_CHECK_EQUAL(*report.currentNumberRow, "2");
    BOOST_CHECK_EQUAL(*readLiveRow(storage, currentNumberKey()), "2");
    // …and so is block 2's header row.
    BOOST_CHECK(
        readLiveRow(storage, std::string(ledger::SYS_NUMBER_2_BLOCK_HEADER) + ":2").has_value());
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::audit::test
