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
 * @file HistoryRollback.h
 * @brief Operator rollback: reverse-apply StateHistory and TrieHistory block by block
 *        (pathdb spec §11, last row of the read-path table)
 *
 * **Offline, and the whole file depends on it.** The node is STOPPED and this runs over its
 * RocksDB directly, through store objects it constructs for the occasion. Those stores' in-memory
 * indexes are empty and stay empty: nothing here calls `readAt`, so nothing needs an index, and
 * nothing calls `publish`, so the `expire(..., Keep)` below leaves no index out of step with the
 * rows it deleted. The node rebuilds its index from the shards that survive on its next start
 * (ReverseHistoryStore::rebuild), which is where the memory side gets its truth back.
 *
 * That obligation is stated here rather than left implicit because `expire` normally comes in a
 * pair: the commit path issues it against a live store and then hands the returned `retired` block
 * to `publish` so the index forgets what the disk forgot (G9). A caller that skips the publish and
 * is NOT offline would leave the index naming shard rows that are gone, and every query landing on
 * one of them would throw HistoryPruned for a height the store should still answer for.
 */
#pragma once

#include "../Errors.h"
#include "../history/HistoryErrors.h"
#include "../history/HistoryRowCodec.h"
#include "../history/HistoryTables.h"
#include "../history/ReverseHistoryStore.h"
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <boost/throw_exception.hpp>
#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace bcos::ledger::mpt::audit
{

/// What one rollbackTo() call did, or — with apply=false — what it would do.
struct RollbackReport
{
    protocol::BlockNumber tip{};
    protocol::BlockNumber target{};
    /// Blocks reverse-applied: tip, tip-1, ... target+1.
    std::size_t blocks{};
    /// Pre-images the state history holds for those blocks.
    std::size_t stateRows{};
    /// Pre-images the trie history holds for those blocks.
    std::size_t trieRows{};
    /// Rows written back and rows deleted. Zero on a dry run — the counts above are what a dry
    /// run reports, and they are read off the meta rows without touching the live plane.
    std::size_t rowsWritten{};
    std::size_t rowsDeleted{};
    bool applied{false};
    /// `s_current_state:current_number` as it reads AFTER the rollback, when the row exists.
    ///
    /// It is NOT restored, and that is settled rather than conditional: the state history captures
    /// exactly the `/apps/` nonce, balance, codeHash and slot rows that the state commitment folds
    /// in (HistoryCommit.h::isHistoricalStateRow), and `s_current_state` is not an account table —
    /// nor could it be, since collectStateHistoryKeys runs at execute time, before Ledger writes
    /// the tip row at all. So this reads whatever it read before the rollback, every time. It is
    /// carried so the caller can quote the CURRENT value in the instruction it gives the operator,
    /// not so it can be compared against the target.
    std::optional<std::string> currentNumberRow{};
};

namespace detail
{

/// Block @p block's meta row in @p Tables, or nullopt when there is none.
///
/// A point read, and the ONLY question the pre-check needs answered: the coverage contract
/// (HistoryTables.h::kBlockHistoryCoverageContract) makes "has a meta row" mean "this block's
/// pre-images were captured", including for a block that changed nothing — which still gets
/// `Meta{shardCount = 1, recordCount = 0}`. So the absence of the row is unambiguous, and the
/// declared `recordCount` that comes back with it is exactly what a dry run counts.
template <history::HistoryTables const& Tables, class Storage>
bcos::task::Task<std::optional<history::BlockMeta>> readBlockMeta(
    Storage& storage, protocol::BlockNumber block)
{
    auto row = co_await bcos::storage2::readOne(
        storage, executor_v1::StateKey{Tables.shard, history::metaRowKey(block)});
    if (!row)
    {
        co_return std::nullopt;
    }
    co_return history::decodeMeta(row->get());
}

/// Reverse-apply one block from ONE history instance: put every recorded pre-image back.
///
/// `readBlock` hands over the block's whole diff as `(key, oldValue | ABSENT)` in one pass, so
/// there is nothing to derive: a record with a value is written back verbatim, a record tagged
/// ABSENT means the key did not exist when the block began and its row is deleted. G4's asymmetry
/// falls out of that structurally rather than being a rule this function has to remember — the
/// only rows it deletes are the ones the history recorded as absent.
///
/// It is also why the earlier "the pre-image is whatever readAt answers for block - 1" indirection
/// is gone: that reached the same records through the query path, which meant the rollback
/// depended on the window guard, the retention boundary and the in-memory index — none of which
/// exist in an offline tool. Reading the block's own rows depends on nothing but the rows.
///
/// @throws MPTInvariantViolation (from readBlock) when the block's meta row is missing, when the
///         shard or record counts disagree with it, or when a row is a deletion sentinel. Each of
///         those means the diff on offer is SHORT, and applying a short diff would leave the rows
///         it lost standing at their post-block values with nothing reporting it (G6).
/// @returns (rows written, rows deleted).
template <history::HistoryTables const& Tables, class Storage>
bcos::task::Task<std::pair<std::size_t, std::size_t>> reverseApplyBlock(
    history::ReverseHistoryStore<Tables> const& store, Storage& storage,
    protocol::BlockNumber block)
{
    auto const recorded = co_await store.readBlock(storage, block);

    std::vector<std::tuple<executor_v1::StateKey, executor_v1::StateValue>> writes;
    std::vector<executor_v1::StateKey> deletes;
    writes.reserve(recorded.records.size());
    for (auto const& [key, oldValue] : recorded.records)
    {
        // A history key IS the physical state key of the row it shadows — "<table>:<rowKey>" —
        // for both instances: ordinary state rows and path-addressed node rows alike. StateKey's
        // string constructor splits it back at the first colon, the same rule StateKeyResolver
        // applies on the way out of RocksDB.
        executor_v1::StateKey stateKey{std::string(key.begin(), key.end())};
        if (!oldValue)
        {
            deletes.push_back(std::move(stateKey));
            continue;
        }
        executor_v1::StateValue entry;
        entry.set(std::string(oldValue->begin(), oldValue->end()));
        writes.emplace_back(std::move(stateKey), std::move(entry));
    }

    auto const written = writes.size();
    auto const removed = deletes.size();
    if (!writes.empty())
    {
        co_await bcos::storage2::writeSome(storage, std::move(writes));
    }
    if (!deletes.empty())
    {
        co_await bcos::storage2::removeSome(storage, std::move(deletes));
    }
    co_return std::pair<std::size_t, std::size_t>{written, removed};
}

}  // namespace detail

/// Roll the state and trie planes of @p storage back to the end of block @p target.
///
/// Blocks are undone newest first: for each block B from @p tip down to `target + 1`, the state
/// pre-images and the trie-node pre-images of B are written back TOGETHER, and only then are B's
/// own history rows deleted. Applying both planes before dropping either block's records is what
/// keeps an interrupted run recoverable: a crash mid-block leaves that block's meta and shard rows
/// intact, so re-running redoes it — the writes are absolute values, so redoing one is a no-op.
///
/// **The node must be stopped.** This writes directly into the state plane with no coordination
/// with a running scheduler, consensus or RPC, and the store objects it uses are its own (see the
/// file header on why no `publish` is needed for the expiries below).
///
/// **Scope of what is rolled back: the state plane and the trie node rows, and nothing else.**
/// Not a hedge — the capture set is decided. HistoryCommit.h::isHistoricalStateRow keeps only the
/// four `/apps/` row kinds the state commitment folds in, so the header, number<->hash,
/// transaction, receipt and nonce-list rows of the blocks being undone are never in the history
/// and are still on disk when this returns. A caller that needs a full block rollback has to deal
/// with those itself.
///
/// Every block in (@p target, @p tip] must have a meta row in BOTH histories, or the whole
/// rollback is refused before the first write: without that check a block whose history was never
/// recorded — a pre-MPT block, a block committed while the depth was 0, a block an expiry already
/// dropped — would be walked past, and the live plane would be left straddling two block heights
/// while the run reported success.
///
/// **The tip row is left for the operator, by decision.** `s_current_state:current_number` is
/// outside the capture set above, so it always still reads the pre-rollback tip when this returns;
/// RollbackReport::currentNumberRow carries that value so a caller can quote it in the instruction
/// it prints. Synthesising it here would mean this tool deciding what "the tip" is on its own,
/// which is Ledger's judgement, not an audit tool's — and it would be only one row of the several
/// a real tip move touches.
///
/// @param stateDepth H_state, @param proofDepth H_proof — the retention depths the node runs
///        with. They are checked, not used: a rollback reads each block's OWN records rather than
///        querying at a height, so the retention window bounds how far back a target can be but
///        plays no part in reading. A depth of 0 means that history was never written at all.
/// @param apply false = dry run: count what would change from the meta rows, write nothing.
/// @throws InvalidHistoryBlock when @p target is negative, not below @p tip, or names a history
///         this node never recorded; HistoryPruned when @p target predates either retention
///         window; MPTInvariantViolation on a missing or damaged block inside the range.
template <class Storage>
    requires history::QueryableStateStorage<Storage> && history::WritableStateStorage<Storage>
bcos::task::Task<RollbackReport> rollbackTo(Storage& storage, protocol::BlockNumber tip,
    protocol::BlockNumber target, protocol::BlockNumber stateDepth,
    protocol::BlockNumber proofDepth, bool apply)
{
    if (target < 0 || target >= tip)
    {
        BOOST_THROW_EXCEPTION(
            history::InvalidHistoryBlock{} << bcos::errinfo_comment(
                "rollback target " + std::to_string(target) +
                " must be a non-negative block below the tip " + std::to_string(tip)));
    }
    // A depth of 0 is not a narrow window, it is no window: HistoryCommit.h writes nothing for a
    // store whose depth is 0, so that history's pre-images were never captured for any block and
    // no target can bring them back. Saying so is the whole point of separating this from the
    // window check below, which reports "target T is older than the retained window (tip N, depth
    // 0)" — true, and an invitation to keep raising T until it works, which it never will.
    for (auto const& [depth, storeName, planeName] :
        {std::tuple{stateDepth, "StateHistory", "state plane"},
            std::tuple{proofDepth, "TrieHistory", "trie node rows"}})
    {
        if (depth <= 0)
        {
            BOOST_THROW_EXCEPTION(
                history::InvalidHistoryBlock{} << bcos::errinfo_comment(
                    "this node retains no " + std::string(storeName) + " (depth " +
                    std::to_string(depth) + "): its pre-images were never recorded, so the " +
                    std::string(planeName) + " cannot be rolled back; the operation is refused"));
        }
    }
    // The oldest block this rollback touches is `target + 1`, whose records are what restore the
    // state as of @p target. Checking the depths up front turns "we rewrote 900 blocks and then
    // hit a pruned one" into a refusal before the first write.
    for (auto const depth : {stateDepth, proofDepth})
    {
        if (target < tip - depth + 1)
        {
            BOOST_THROW_EXCEPTION(
                history::HistoryPruned{} << bcos::errinfo_comment(
                    "rollback target " + std::to_string(target) +
                    " is older than the retained history window (tip " + std::to_string(tip) +
                    ", depth " + std::to_string(depth) + ")"));
        }
    }

    // Every block being undone must actually HAVE a meta row, in both histories, before anything
    // is written — and the counts those rows declare are what the dry run reports, so the pre-check
    // and the dry run are one pass rather than two.
    //
    // The damage the check prevents is silent: skipping one block leaves the live plane a mixture
    // of two block heights, and nothing downstream notices — the caller's own post-rollback
    // re-audit compares the tree against the TARGET block's root, which a partial rollback of the
    // state plane can still satisfy when the skipped block touched no trie node. It happens for
    // free with the CLI's default retention depth of 128 against a node configured shorter, and it
    // happens for real on a B.10 ② hole.
    std::map<protocol::BlockNumber, std::pair<std::size_t, std::size_t>> declared;
    for (auto block = target + 1; block <= tip; ++block)
    {
        auto const stateMeta =
            co_await detail::readBlockMeta<history::kStateHistory>(storage, block);
        auto const trieMeta = co_await detail::readBlockMeta<history::kTrieHistory>(storage, block);
        for (auto const& [meta, name] : {std::pair{std::cref(stateMeta), "StateHistory"},
                 std::pair{std::cref(trieMeta), "TrieHistory"}})
        {
            if (!meta.get())
            {
                BOOST_THROW_EXCEPTION(
                    MPTInvariantViolation{} << bcos::errinfo_comment(
                        "rollback " + std::to_string(tip) + " -> " + std::to_string(target) +
                        " refused: block " + std::to_string(block) + " has no " + name +
                        " meta row, so its pre-images cannot be replayed. Nothing was written. "
                        "Check the retention depths, and run `mpt-audit history` for the full "
                        "picture."));
            }
        }
        declared.emplace(block,
            std::pair<std::size_t, std::size_t>{stateMeta->recordCount, trieMeta->recordCount});
    }

    RollbackReport report{.tip = tip, .target = target, .applied = apply};
    // One store per plane for the whole walk. They exist for their disk-facing members only:
    // readBlock and expire never touch the index, so these two objects carry no state between
    // blocks and nothing is ever published into them (file header).
    history::StateHistoryStore stateStore;
    history::TrieHistoryStore trieStore;

    for (auto block = tip; block > target; --block)
    {
        ++report.blocks;
        if (!apply)
        {
            auto const& [stateRecords, trieRecords] = declared.at(block);
            report.stateRows += stateRecords;
            report.trieRows += trieRecords;
            continue;
        }

        auto const stateApplied = co_await detail::reverseApplyBlock(stateStore, storage, block);
        auto const trieApplied = co_await detail::reverseApplyBlock(trieStore, storage, block);
        report.stateRows += stateApplied.first + stateApplied.second;
        report.trieRows += trieApplied.first + trieApplied.second;
        report.rowsWritten += stateApplied.first + trieApplied.first;
        report.rowsDeleted += stateApplied.second + trieApplied.second;

        // The block is undone; its own history is what goes last (spec B.5's ordering, applied to
        // rollback: while the meta row survives, re-running the block is possible).
        //
        // RetentionBoundary::Keep — the default, spelled out because it is the load-bearing half
        // of the difference from the commit path. This discards from the TOP, so the oldest block
        // the store can answer for has not moved; advancing here would also refuse the very next
        // step of this walk.
        co_await stateStore.expire(storage, storage, block, history::RetentionBoundary::Keep);
        co_await trieStore.expire(storage, storage, block, history::RetentionBoundary::Keep);
    }

    if (apply)
    {
        auto currentNumber = co_await bcos::storage2::readOne(storage,
            executor_v1::StateKey{ledger::SYS_CURRENT_STATE, ledger::SYS_KEY_CURRENT_NUMBER});
        if (currentNumber)
        {
            auto const value = currentNumber->get();
            report.currentNumberRow = std::string(value.begin(), value.end());
        }
    }

    co_return report;
}

}  // namespace bcos::ledger::mpt::audit
