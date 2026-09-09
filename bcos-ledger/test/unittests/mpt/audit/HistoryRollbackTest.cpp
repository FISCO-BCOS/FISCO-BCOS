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
#include <deque>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>

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

/// Three committed blocks, both histories recorded, with the state plane standing at block 3.
///
///   key                              b1        b2        b3
///   /apps/a:x                        "v1"      "v2"      "v3"
///   /apps/a:y                        -         "w2"      deleted
///   s_current_state:current_number   "1"       "2"       "3"
///   /mptp/a:<root position>          "n1"      "n2"      "n3"
struct RollbackFixture
{
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
        bcos::task::syncWait(history::StateHistoryStore::put(
            storage, block, stateDiff.entries(), history::test::kWideShardCap));

        history::test::Diff trieDiff;
        trieDiff.change(nodeKey, nodeOld);
        bcos::task::syncWait(history::TrieHistoryStore::put(
            storage, block, trieDiff.entries(), history::test::kWideShardCap));

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
    // Three state keys and one trie key per block, two blocks.
    BOOST_CHECK_EQUAL(report.stateRows, 6);
    BOOST_CHECK_EQUAL(report.trieRows, 2);
    BOOST_CHECK_EQUAL(report.rowsWritten, 0);
    BOOST_CHECK_EQUAL(report.rowsDeleted, 0);

    // Nothing moved.
    BOOST_CHECK_EQUAL(*readLiveRow(fixture.storage, "/apps/a:x"), "v3");
    BOOST_CHECK_EQUAL(
        bcos::task::syncWait(history::StateHistoryStore::keysOfBlock(fixture.storage, 3)).size(),
        3);
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
    BOOST_CHECK(
        bcos::task::syncWait(history::StateHistoryStore::keysOfBlock(fixture.storage, 3)).empty());
    BOOST_CHECK(
        bcos::task::syncWait(history::TrieHistoryStore::keysOfBlock(fixture.storage, 2)).empty());
    BOOST_CHECK_EQUAL(
        bcos::task::syncWait(history::StateHistoryStore::keysOfBlock(fixture.storage, 1)).size(),
        3);
}

BOOST_AUTO_TEST_CASE(rollingBackOneBlockStopsAtTheBlockBelow)
{
    RollbackFixture fixture;
    fixture.rollback(2, /*apply=*/true);

    BOOST_CHECK_EQUAL(*readLiveRow(fixture.storage, "/apps/a:x"), "v2");
    BOOST_CHECK_EQUAL(*readLiveRow(fixture.storage, "/apps/a:y"), "w2");
    BOOST_CHECK_EQUAL(*readLiveRow(fixture.storage, fixture.nodeKey), "n2");
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

    // Block 1: two accounts.
    std::map<bcos::h256, bcos::bytes> first;
    first[accountKeyHash(mpt::test::makeAddress(0x01))] = Account{}.encode();
    first[accountKeyHash(mpt::test::makeAddress(0x02))] = Account{}.encode();
    auto const rootAtOne = mpt::test::seedTrieFlushed(nodes, emptyRootHash(), first).root;
    landNodeRowsOnFlat(nodes, flat);
    writeLiveRow(flat, currentNumberKey(), "1");
    history::test::Diff blockOne;
    blockOne.change(currentNumberKey(), std::nullopt);
    bcos::task::syncWait(
        history::StateHistoryStore::put(flat, 1, blockOne.entries(), history::test::kWideShardCap));
    bcos::task::syncWait(history::TrieHistoryStore::put(flat, 1, {}, history::test::kWideShardCap));

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
    bcos::task::syncWait(
        history::TrieHistoryStore::put(flat, 2, entries, history::test::kWideShardCap));

    history::test::Diff blockTwo;
    blockTwo.change(currentNumberKey(), "1"sv);
    bcos::task::syncWait(
        history::StateHistoryStore::put(flat, 2, blockTwo.entries(), history::test::kWideShardCap));
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

/// NEGATIVE CONTROL — a block inside the range whose manifest is gone must abort the WHOLE
/// rollback before a single row moves.
///
/// keysOfBlock() answers an expired block and a block that changed nothing with the same empty
/// vector, so without the manifest pre-check block 2 would look like "nothing to undo": block 3
/// alone would be reverse-applied and the live plane would be left straddling two block heights,
/// reported as a success.
BOOST_AUTO_TEST_CASE(missingManifestInsideTheRangeRefusesTheWholeRollback)
{
    RollbackFixture fixture;
    bcos::task::syncWait(history::StateHistoryStore::expire(fixture.storage, fixture.storage, 2));

    BOOST_CHECK_EXCEPTION(fixture.rollback(1, /*apply=*/true), MPTInvariantViolation,
        [](MPTInvariantViolation const& error) {
            std::string const message = boost::diagnostic_information(error);
            BOOST_TEST_MESSAGE("rollback refused: " << message);
            return message.find("block 2 has no StateHistory manifest") != std::string::npos &&
                   message.find("Nothing was written") != std::string::npos;
        });

    // Zero rows written: block 3 was not touched even though its own history is intact.
    BOOST_CHECK_EQUAL(*readLiveRow(fixture.storage, "/apps/a:x"), "v3");
    BOOST_CHECK(!readLiveRow(fixture.storage, "/apps/a:y").has_value());
    BOOST_CHECK_EQUAL(*readLiveRow(fixture.storage, fixture.nodeKey), "n3");
    BOOST_CHECK_EQUAL(*readLiveRow(fixture.storage, currentNumberKey()), "3");
    BOOST_CHECK_EQUAL(
        bcos::task::syncWait(history::StateHistoryStore::keysOfBlock(fixture.storage, 3)).size(),
        3);
}

/// The dry run refuses too — that is where an operator looks first, so it is where the refusal has
/// to land.
BOOST_AUTO_TEST_CASE(missingManifestInsideTheRangeAlsoRefusesTheDryRun)
{
    RollbackFixture fixture;
    bcos::task::syncWait(history::TrieHistoryStore::expire(fixture.storage, fixture.storage, 3));

    BOOST_CHECK_EXCEPTION(fixture.rollback(1, /*apply=*/false), MPTInvariantViolation,
        [](MPTInvariantViolation const& error) {
            return std::string(boost::diagnostic_information(error))
                       .find("block 3 has no TrieHistory manifest") != std::string::npos;
        });
}

/// A block that genuinely changed nothing is NOT a missing manifest: put() always writes shard 0,
/// so the block is present with an empty key list and the rollback proceeds.
BOOST_AUTO_TEST_CASE(blockThatChangedNothingDoesNotLookLikeAHole)
{
    history::test::HistoryMemStorage storage;
    history::test::Diff first;
    first.change("/apps/a:x"sv, std::nullopt);
    bcos::task::syncWait(
        history::StateHistoryStore::put(storage, 1, first.entries(), history::test::kWideShardCap));
    bcos::task::syncWait(
        history::TrieHistoryStore::put(storage, 1, first.entries(), history::test::kWideShardCap));

    history::test::Diff const empty;
    for (protocol::BlockNumber block = 2; block <= 3; ++block)
    {
        bcos::task::syncWait(history::StateHistoryStore::put(
            storage, block, empty.entries(), history::test::kWideShardCap));
        bcos::task::syncWait(history::TrieHistoryStore::put(
            storage, block, empty.entries(), history::test::kWideShardCap));
    }
    writeLiveRow(storage, "/apps/a:x", "v1");

    auto const report = bcos::task::syncWait(rollbackTo(storage, 3, 1, 8, 8, /*apply=*/true));
    BOOST_CHECK_EQUAL(report.blocks, 2);
    BOOST_CHECK_EQUAL(report.rowsWritten + report.rowsDeleted, 0);
    BOOST_CHECK_EQUAL(*readLiveRow(storage, "/apps/a:x"), "v1");
}

/// NEGATIVE CONTROL — a block whose manifest lists a key it has no index row for cannot be
/// reverse-applied, and the rollback must stop rather than leave that row at its post-block value
/// (G6).
BOOST_AUTO_TEST_CASE(holeInThePreimageChainStopsTheRollback)
{
    RollbackFixture fixture;
    bcos::task::syncWait(bcos::storage2::removeOne(
        fixture.storage, executor_v1::StateKey{history::kStateHistory.index,
                             history::indexRowKey(history::test::makeBytes("/apps/a:x"sv), 3)}));

    BOOST_CHECK_THROW(fixture.rollback(1, /*apply=*/true), MPTInvariantViolation);
}

/// The CLI's `--yes` leg, end to end, against the storage production runs on.
///
/// Two real trie versions, block 2's PathMergeResult::preimages fed to TrieHistory, an /apps/ row
/// in the state history shaped the way isHistoricalStateRow captures them, headers for both blocks
/// and a seeded retention boundary — a store that looks like a node's, not like a test's. One copy
/// is rolled back here with assertions; a second identical copy is left on disk under
/// MPT_AUDIT_TEST_DB_DIR so `mpt-audit rollback --to 1 --yes` has a pristine store to run against.
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
    bcos::task::syncWait(history::StateHistoryStore::put(
        storage, 1, blockOneState.entries(), history::kManifestShardByteCap));
    bcos::task::syncWait(
        history::TrieHistoryStore::put(storage, 1, {}, history::kManifestShardByteCap));
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
    bcos::task::syncWait(
        history::TrieHistoryStore::put(storage, 2, trieEntries, history::kManifestShardByteCap));

    built.newAccountRow = "/apps/0300000000000000000000000000000000000000:nonce";
    history::test::Diff blockTwoState;
    blockTwoState.change(built.newAccountRow, std::nullopt);
    bcos::task::syncWait(history::StateHistoryStore::put(
        storage, 2, blockTwoState.entries(), history::kManifestShardByteCap));
    writeLiveRow(storage, built.newAccountRow, "1");

    // The chain metadata the CLI reads: a header per block, and the tip.
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
    }
    writeLiveRow(storage, currentNumberKey(), "2");
    return built;
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
            // expiry that dropped rows without advancing the boundary. Oldest manifest 2, boundary
            // still 0, so the store claims to answer for block 1 with nothing left to answer from:
            // the direction of B.10 ④ that produces silently wrong historical reads, and the one
            // the CLI has to print the `detail` text for.
            AuditRocksDb badBoundary(base + "-badboundary", true);
            RocksDbStateStorage storage(*badBoundary.db,
                bcos::storage2::rocksdb::StateKeyResolver{},
                bcos::storage2::rocksdb::StateValueResolver{});
            buildRocksDbRollbackStore(storage);
            bcos::task::syncWait(history::StateHistoryStore::expire(storage, storage, 1));

            auto const report = bcos::task::syncWait(auditStateHistory(storage, 2, 8, 1));
            BOOST_CHECK_EQUAL(report.oldestRetained, 2);
            BOOST_REQUIRE(report.retentionBoundary.has_value());
            BOOST_CHECK_EQUAL(*report.retentionBoundary, 0);
            BOOST_REQUIRE(!report.findings.empty());
            BOOST_CHECK(report.findings.back().detail.find(
                            "claims heights this store no longer holds") != std::string::npos);
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

    // Standing at block 2, and both histories audit clean against their own metadata.
    BOOST_CHECK_NO_THROW(runPathTreeAudit(storage, built.rootAtTwo));
    BOOST_CHECK(bcos::task::syncWait(auditStateHistory(storage, 2, 8, 1)).consistent());
    BOOST_CHECK(bcos::task::syncWait(auditTrieHistory(storage, 2, 8, 1)).consistent());

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
