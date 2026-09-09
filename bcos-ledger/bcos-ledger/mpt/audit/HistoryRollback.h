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
 */
#pragma once

#include "../Errors.h"
// detail::scanManifests — "which blocks have a manifest at all", the question keysOfBlock
// cannot answer (it returns empty for a missing manifest and for an empty one alike).
#include "../history/HistoryErrors.h"
#include "../history/HistoryTables.h"
#include "../history/ReverseHistoryStore.h"
#include "HistoryAudit.h"
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
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
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
    /// run reports, and they are read off the manifests without touching the live plane.
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
    std::optional<std::string> currentNumberRow;
};

namespace detail
{

/// Reverse-apply one block from ONE history instance, then drop that block's history rows.
///
/// The pre-image of key k for block B is fetched as "the value k held at block B-1", which is
/// exactly what readAt answers, and reusing it rather than reading the (k, B) index row directly
/// is deliberate: readAt owns the window guard, the tag decoding and the fail-loud behaviour, and
/// a second copy of that decoding here would be a second thing to keep in step with the layout.
///
/// The seek readAt performs lands on (k, B) precisely because rollback runs from the tip
/// DOWNWARDS and deletes each block's history as it finishes with it — every row newer than B is
/// already gone by the time B is processed, so the first row at or after (k, B) is (k, B) itself.
/// If it is not there, readAt reports HistoryUseCurrent, which for a key the manifest just listed
/// can only mean the index row is missing; that is a hole, and it stops the rollback (G6) rather
/// than silently leaving the row at its post-block value.
///
/// @returns (rows written, rows deleted).
template <history::HistoryTables const& Tables, class Storage>
bcos::task::Task<std::pair<std::size_t, std::size_t>> reverseApplyBlock(Storage& storage,
    protocol::BlockNumber block, protocol::BlockNumber tip, protocol::BlockNumber depth)
{
    using Store = history::ReverseHistoryStore<Tables>;

    auto const keys = co_await Store::keysOfBlock(storage, block);

    std::vector<std::tuple<executor_v1::StateKey, executor_v1::StateValue>> writes;
    std::vector<executor_v1::StateKey> deletes;
    writes.reserve(keys.size());
    for (auto const& key : keys)
    {
        auto value = co_await Store::readAt(storage, key, block - 1, tip, depth);
        // A history key IS the physical state key of the row it shadows — "<table>:<rowKey>" —
        // for both instances: ordinary state rows and path-addressed node rows alike. StateKey's
        // string constructor splits it back at the first colon, the same rule StateKeyResolver
        // applies on the way out of RocksDB.
        executor_v1::StateKey stateKey{std::string(key.begin(), key.end())};
        if (std::holds_alternative<history::HistoryUseCurrent>(value))
        {
            BOOST_THROW_EXCEPTION(
                MPTInvariantViolation{} << bcos::errinfo_comment(
                    "rollback: block " + std::to_string(block) +
                    " lists a key in its manifest but holds no index row for it; the pre-image "
                    "chain has a hole and the rollback cannot be completed"));
        }
        if (std::holds_alternative<history::HistoryAbsent>(value))
        {
            deletes.push_back(std::move(stateKey));
            continue;
        }
        auto& bytes = std::get<bcos::bytes>(value);
        executor_v1::StateValue entry;
        entry.set(std::string(bytes.begin(), bytes.end()));
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

/// Keys one block's manifest lists, without reading a single pre-image — what a dry run counts.
template <history::HistoryTables const& Tables, class Storage>
bcos::task::Task<std::size_t> countBlockKeys(Storage& storage, protocol::BlockNumber block)
{
    auto const keys = co_await history::ReverseHistoryStore<Tables>::keysOfBlock(storage, block);
    co_return keys.size();
}

}  // namespace detail

/// Roll the state and trie planes of @p storage back to the end of block @p target.
///
/// Blocks are undone newest first: for each block B from @p tip down to `target + 1`, the state
/// pre-images and the trie-node pre-images of B are written back TOGETHER, and only then is B's
/// own history deleted. Applying both planes before dropping either block's records is what keeps
/// an interrupted run recoverable: a crash mid-block leaves that block's manifests and index rows
/// intact, so re-running redoes it — the writes are absolute values, so redoing one is a no-op.
///
/// **The node must be stopped.** This writes directly into the state plane with no coordination
/// with a running scheduler, consensus or RPC.
///
/// **Scope of what is rolled back: the state plane and the trie node rows, and nothing else.**
/// Not a hedge — the capture set is decided. HistoryCommit.h::isHistoricalStateRow keeps only the
/// four `/apps/` row kinds the state commitment folds in, so the header, number<->hash,
/// transaction, receipt and nonce-list rows of the blocks being undone are never in the history
/// and are still on disk when this returns. A caller that needs a full block rollback has to deal
/// with those itself.
///
/// Every block in (@p target, @p tip] must have a manifest in BOTH histories, or the whole
/// rollback is refused before the first write: a missing manifest is indistinguishable from an
/// empty one at the keysOfBlock level, so proceeding would silently skip that block and leave the
/// live plane straddling two block heights.
///
/// **The tip row is left for the operator, by decision.** `s_current_state:current_number` is
/// outside the capture set above, so it always still reads the pre-rollback tip when this returns;
/// RollbackReport::currentNumberRow carries that value so a caller can quote it in the instruction
/// it prints. Synthesising it here would mean this tool deciding what "the tip" is on its own,
/// which is Ledger's judgement, not an audit tool's — and it would be only one row of the several
/// a real tip move touches.
///
/// @param stateDepth H_state, @param proofDepth H_proof — the retention depths the node runs
///        with. They bound how far back a rollback can reach: the pre-image of the oldest block
///        being undone is read AT block @p target, so @p target must still be inside both
///        windows.
/// @param apply false = dry run: count what would change from the manifests, write nothing.
/// @throws InvalidHistoryBlock when @p target is negative or not below @p tip; HistoryPruned when
///         @p target predates either retention window; MPTInvariantViolation on a hole in either
///         history chain.
template <class Storage>
    requires history::SeekableStateStorage<Storage> && history::WritableStateStorage<Storage> &&
             bcos::storage2::ReadableStorage<Storage, executor_v1::StateKey>
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
    // The oldest read this rollback performs is "the value at block target", so target itself has
    // to be inside both windows. Checking up front turns "we rewrote 900 blocks and then hit a
    // pruned one" into a refusal before the first write.
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

    // Every block being undone must actually HAVE a manifest, in both histories, before anything
    // is written.
    //
    // keysOfBlock() answers "no manifest" and "manifest listing nothing" identically — an empty
    // vector — so without this check a block whose history is missing looks like a block that
    // changed nothing, and the rollback quietly skips it and reports success for the range. That
    // happens for free with the CLI's default retention depth of 128 against a node configured
    // shorter, and it happens for real on a B.10 ② hole. The damage is silent: the live plane ends
    // up a mixture of two block heights, and nothing downstream notices — the caller's own
    // post-rollback re-audit compares the tree against the TARGET block's root, which a partial
    // rollback of the state plane can still satisfy when the skipped block touched no trie node.
    auto const stateManifests = co_await detail::scanManifests<history::kStateHistory>(storage);
    auto const trieManifests = co_await detail::scanManifests<history::kTrieHistory>(storage);
    for (auto block = target + 1; block <= tip; ++block)
    {
        for (auto const& [manifests, name] : {std::pair{std::cref(stateManifests), "StateHistory"},
                 std::pair{std::cref(trieManifests), "TrieHistory"}})
        {
            if (!manifests.get().contains(block))
            {
                BOOST_THROW_EXCEPTION(
                    MPTInvariantViolation{} << bcos::errinfo_comment(
                        "rollback " + std::to_string(tip) + " -> " + std::to_string(target) +
                        " refused: block " + std::to_string(block) + " has no " + name +
                        " manifest, so its pre-images cannot be replayed. Nothing was written. "
                        "Check the retention depths, and run `mpt-audit history` for the full "
                        "picture."));
            }
        }
    }

    RollbackReport report{.tip = tip, .target = target, .applied = apply};
    for (auto block = tip; block > target; --block)
    {
        ++report.blocks;
        if (!apply)
        {
            auto const stateKeys =
                co_await detail::countBlockKeys<history::kStateHistory>(storage, block);
            auto const trieKeys =
                co_await detail::countBlockKeys<history::kTrieHistory>(storage, block);
            report.stateRows += stateKeys;
            report.trieRows += trieKeys;
            continue;
        }

        auto const stateApplied = co_await detail::reverseApplyBlock<history::kStateHistory>(
            storage, block, tip, stateDepth);
        auto const trieApplied = co_await detail::reverseApplyBlock<history::kTrieHistory>(
            storage, block, tip, proofDepth);
        report.stateRows += stateApplied.first + stateApplied.second;
        report.trieRows += trieApplied.first + trieApplied.second;
        report.rowsWritten += stateApplied.first + trieApplied.first;
        report.rowsDeleted += stateApplied.second + trieApplied.second;

        // The block is undone; its own history is what goes last (spec B.5's ordering, applied to
        // rollback: while the manifest survives, re-running the block is possible).
        co_await history::StateHistoryStore::expire(storage, storage, block);
        co_await history::TrieHistoryStore::expire(storage, storage, block);
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
