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
 * @file EthereumChainRollback.h
 * @brief EL-mode shallow-reorg support: a per-block rollback journal plus the executor
 *        that rewinds the committed state from head h back to a target r within the
 *        configured reorg window.
 *
 * Design (Phase 3 of the EL-mode reorg plan):
 *  - Journal: the commit of every block N writes ONE row into SYS_ROLLBACK_JOURNAL
 *    (key = block number) recording, for every flat-state row the block's executed view
 *    dirtied, the row's PRE-BLOCK value (absent = the row did not exist). The "/mpt/"
 *    node rows are content-addressed and shared across forks, so they are deliberately
 *    NOT journaled — rolling back flat state never requires touching them. The two
 *    SYS_CURRENT_STATE counters (total/failed transaction count) are bumped by the
 *    ledger prewrite OUTSIDE the executed view, so their pre-block values are appended
 *    to the journal unconditionally. The journal row lands in the block's own prewrite
 *    buffer — the SAME WriteBatch as the block data — so a crash can never leave a
 *    committed block without its journal (or the reverse).
 *  - Retention: the commit of block N deletes journal row N - reorgWindow. Rolling back
 *    to r needs journals r+1..head, so the deepest supported target is head - reorgWindow.
 *  - Rollback: a read-only preflight (depth within the window, every journal present —
 *    refusing LOUDLY otherwise, never a partial rewind) then ONE MutableStorage batch
 *    merged straight into the backends via mergeToBackends: restore the oldest journaled
 *    value of every touched row (lowest block first wins), restore the counters, set
 *    current_number = r, and delete the rolled-back blocks' number-keyed ledger rows
 *    (number->hash, hash->number, header, txs metadata, nonces, withdrawals, journal).
 *    The hash->number rows MUST go: the Engine API newPayload idempotence check resolves
 *    a payload's number through them, and a stale row would answer VALID for a block the
 *    canonical chain no longer contains, wedging the CL sync at the ledger hole.
 *    SYS_HASH_2_TX / SYS_HASH_2_RECEIPT stay: they are content-addressed and harmless
 *    for blocks that never become canonical (the same treatment reorgs get on every
 *    Ethereum client).
 *  - MPT pruning interaction: with the startup check mpt_prune_window >= reorg_window,
 *    every node reachable from a rollback target's state root is still on disk (a node
 *    deleted at block b was obsoleted at b - pruneWindow <= head - reorgWindow <= r, so
 *    root(r >= that) cannot reference it). The pruner rebuilds its in-memory counts from
 *    the new head after a rollback (CommitObserver::coOnRollback) — with pruning enabled
 *    that costs a full head-trie walk per reorg, the accepted trade-off for exact
 *    self-healing; with the default (pruning disabled, NoopCommitObserver) the hook is a
 *    no-op and rollback costs nothing beyond the batch itself.
 */
#pragma once

#include "bcos-codec/rlp/RLPDecode.h"
#include "bcos-codec/rlp/RLPEncode.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-storage/KeyPrefixes.h"
#include "bcos-task/Task.h"
#include <boost/throw_exception.hpp>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace bcos::scheduler_v1
{

/// One journaled row: the pre-block value of (table, key); nullopt oldValue = the row did
/// not exist before the block, so a rollback DELETES it.
struct RollbackJournalEntry
{
    std::string table;
    std::string key;
    std::optional<bcos::bytes> oldValue;
};

/// ADL hooks so the shared RLP codec (bcos-codec/rlp) handles RollbackJournalEntry inside
/// vectors/optionals: an entry encodes as the list [table, key, oldValue?] — the optional
/// is simply absent from the list tail when the row did not exist.
inline size_t length(RollbackJournalEntry const& entry) noexcept
{
    return bcos::codec::rlp::length(entry.table, entry.key, entry.oldValue);
}
inline void encode(bcos::bytes& to, RollbackJournalEntry const& entry) noexcept
{
    bcos::codec::rlp::encode(to, entry.table, entry.key, entry.oldValue);
}
inline void decode(bcos::bytesRef& from, RollbackJournalEntry& entry)
{
    bcos::codec::rlp::decode(from, entry.table, entry.key, entry.oldValue);
}

/// One block's journal: every dirtied flat-state row's pre-block value, plus the two
/// transaction counters' pre-block values (appended by the capture unconditionally).
struct RollbackJournal
{
    std::vector<RollbackJournalEntry> entries;
};

inline bcos::bytes encodeRollbackJournal(RollbackJournal const& journal)
{
    bcos::bytes out;
    bcos::codec::rlp::encode(out, journal.entries);
    return out;
}

/// @throws bcos::codec::rlp::RlpDecodeException on malformed input — a corrupted journal
///         row is local disk corruption, surfaced to the rollback caller which refuses.
inline RollbackJournal decodeRollbackJournal(bcos::bytesConstRef encoded)
{
    RollbackJournal journal;
    bcos::codec::rlp::decodeExact(encoded, journal.entries);
    return journal;
}

/// Capture the rollback journal of the block whose executed (but not yet pushed) view is
/// @p view: every dirty row of the view's top mutable layer OUTSIDE the content-addressed
/// "/mpt/" node table, plus the two ledger counters the commit's prewrite bumps outside
/// the view. Pre-block values are batch-read from @p committed — a COMMITTED-plane view
/// (MultiLayerStorage::forkCommitted), so in-flight layers of other blocks can never leak
/// into the journal. The caller runs on the commit path under the commit mutex, so the
/// committed plane is exactly the parent block's state.
template <class ViewType, class CommittedStorage>
task::Task<RollbackJournal> captureRollbackJournal(ViewType& view, CommittedStorage& committed)
{
    RollbackJournal journal;
    auto iterator = co_await bcos::storage2::range(mutableStorage(view));
    while (auto keyValue = co_await iterator.next())
    {
        auto const& [stateKey, dataValue] = *keyValue;
        executor_v1::StateKeyView const keyView{stateKey};
        if (keyView.m_table == bcos::storage2::kMPTTable)
        {
            continue;
        }
        journal.entries.push_back(RollbackJournalEntry{
            std::string(keyView.m_table), std::string(keyView.m_key), std::nullopt});
    }
    // The counters are bumped by the ledger prewrite (into the prewrite buffer), never
    // through the executed view — append them unconditionally so the rollback can restore
    // them from the DEEPEST rolled-back block's journal.
    journal.entries.push_back(RollbackJournalEntry{std::string(ledger::SYS_CURRENT_STATE),
        std::string(ledger::SYS_KEY_TOTAL_TRANSACTION_COUNT), std::nullopt});
    journal.entries.push_back(RollbackJournalEntry{std::string(ledger::SYS_CURRENT_STATE),
        std::string(ledger::SYS_KEY_TOTAL_FAILED_TRANSACTION), std::nullopt});

    std::vector<executor_v1::StateKey> keys;
    keys.reserve(journal.entries.size());
    for (auto const& entry : journal.entries)
    {
        keys.emplace_back(entry.table, entry.key);
    }
    auto oldEntries = co_await storage2::readSome(committed, keys);
    for (size_t i = 0; i < oldEntries.size(); ++i)
    {
        if (oldEntries[i])
        {
            auto const value = oldEntries[i]->get();
            journal.entries[i].oldValue = bcos::bytes(value.begin(), value.end());
        }
    }
    co_return journal;
}

/// Persist @p journal as block @p blockNumber's row into the commit's prewrite buffer (the
/// same WriteBatch as the block data — the crash-atomicity contract), and prune the row
/// that just left the reorg window (block blockNumber - reorgWindow, when positive).
template <class Storage>
task::Task<void> writeRollbackJournalRows(Storage& prewriteStorage,
    protocol::BlockNumber blockNumber, RollbackJournal const& journal, int64_t reorgWindow)
{
    storage::Entry journalEntry;
    journalEntry.set(encodeRollbackJournal(journal));
    co_await storage2::writeOne(prewriteStorage,
        executor_v1::StateKey{ledger::SYS_ROLLBACK_JOURNAL, std::to_string(blockNumber)},
        std::move(journalEntry));
    auto const expired = blockNumber - reorgWindow;
    if (expired >= 1)
    {
        co_await storage2::removeOne(prewriteStorage,
            executor_v1::StateKey{ledger::SYS_ROLLBACK_JOURNAL, std::to_string(expired)});
    }
}

/// Thrown when a rollback cannot be served: the target is not behind the head, the depth
/// exceeds the reorg window, or a needed journal row is missing (pruned by the window, or
/// never captured — e.g. blocks committed by the engine built-lane, which keeps no
/// journal). The caller classifies this as "reorg too deep — resync", never as a peer
/// fault.
struct RollbackRefused : public std::runtime_error
{
    using std::runtime_error::runtime_error;
};

struct RollbackResult
{
    protocol::BlockNumber oldHead;
    protocol::BlockNumber newHead;
};

/// Rewind the committed chain from the current head to @p targetNumber (exclusive of the
/// rolled-back range [targetNumber+1, head]).
///
/// Preflight is fully read-only and refuses LOUDLY (RollbackRefused) before any write;
/// the apply is ONE MutableStorage batch merged via mergeToBackends (no interaction with
/// the pending-layer deque — the caller guarantees it is empty, which the serialized
/// commit path under the verifier's commit mutex provides).
///
/// @tparam GlobalStateStorage MultiLayerStorage-like: forkCommitted()/mergeToBackends()/
///         MutableStorage
template <class GlobalStateStorage>
task::Task<RollbackResult> rollbackCommittedChain(GlobalStateStorage& globalStateStorage,
    protocol::BlockNumber targetNumber, int64_t reorgWindow)
{
    auto committed = globalStateStorage.forkCommitted();
    auto const head = co_await ledger::getCurrentBlockNumber(committed, ledger::fromStorage);
    if (targetNumber < 0 || targetNumber >= head)
    {
        BOOST_THROW_EXCEPTION(RollbackRefused{"rollback target " + std::to_string(targetNumber) +
                                              " is not behind the head " +
                                              std::to_string(head)});
    }
    auto const depth = head - targetNumber;
    if (reorgWindow <= 0 || depth > reorgWindow)
    {
        BOOST_THROW_EXCEPTION(
            RollbackRefused{"rollback depth " + std::to_string(depth) + " (head " +
                            std::to_string(head) + " -> target " + std::to_string(targetNumber) +
                            ") exceeds the reorg window " + std::to_string(reorgWindow)});
    }

    // Preflight + load: journals targetNumber+1 .. head, every row must be present.
    std::vector<RollbackJournal> journals;
    journals.reserve(static_cast<size_t>(depth));
    for (auto block = targetNumber + 1; block <= head; ++block)
    {
        auto entry = co_await storage2::readOne(committed,
            executor_v1::StateKeyView{ledger::SYS_ROLLBACK_JOURNAL, std::to_string(block)});
        if (!entry)
        {
            BOOST_THROW_EXCEPTION(
                RollbackRefused{"rollback journal missing for block " + std::to_string(block) +
                                " (pruned by the window, or committed without journaling — "
                                "cannot rewind safely)"});
        }
        auto const raw = entry->get();
        journals.push_back(decodeRollbackJournal(bcos::bytesConstRef(
            reinterpret_cast<bcos::byte const*>(raw.data()), raw.size())));
    }

    typename GlobalStateStorage::MutableStorage batch;

    // Restore the journaled pre-block values, LOWEST block first: the first writer of a
    // key in [target+1, head] order holds the value the target block's state had.
    std::set<executor_v1::StateKey> restored;
    for (auto const& journal : journals)
    {
        for (auto const& entry : journal.entries)
        {
            executor_v1::StateKey stateKey{entry.table, entry.key};
            if (!restored.insert(stateKey).second)
            {
                continue;
            }
            if (entry.oldValue)
            {
                storage::Entry restoreEntry;
                restoreEntry.set(*entry.oldValue);
                co_await storage2::writeOne(batch, std::move(stateKey), std::move(restoreEntry));
            }
            else
            {
                co_await storage2::removeOne(batch, std::move(stateKey));
            }
        }
    }

    // Delete the rolled-back blocks' number-keyed ledger rows. The hash->number row goes
    // too (read the hash first): the Engine API's newPayload idempotence check resolves a
    // known payload through it and would otherwise report VALID for a block the canonical
    // chain no longer contains.
    for (auto block = targetNumber + 1; block <= head; ++block)
    {
        auto const numberStr = std::to_string(block);
        if (auto hashEntry = co_await storage2::readOne(
                committed, executor_v1::StateKeyView{ledger::SYS_NUMBER_2_HASH, numberStr}))
        {
            auto const hashBinary = hashEntry->get();
            co_await storage2::removeOne(batch,
                executor_v1::StateKey{ledger::SYS_HASH_2_NUMBER,
                    std::string_view(hashBinary.data(), hashBinary.size())});
        }
        co_await storage2::removeOne(
            batch, executor_v1::StateKey{ledger::SYS_NUMBER_2_HASH, numberStr});
        co_await storage2::removeOne(
            batch, executor_v1::StateKey{ledger::SYS_NUMBER_2_BLOCK_HEADER, numberStr});
        co_await storage2::removeOne(
            batch, executor_v1::StateKey{ledger::SYS_NUMBER_2_TXS, numberStr});
        co_await storage2::removeOne(
            batch, executor_v1::StateKey{ledger::SYS_BLOCK_NUMBER_2_NONCES, numberStr});
        co_await storage2::removeOne(
            batch, executor_v1::StateKey{ledger::SYS_NUMBER_2_WITHDRAWALS, numberStr});
        co_await storage2::removeOne(
            batch, executor_v1::StateKey{ledger::SYS_ROLLBACK_JOURNAL, numberStr});
    }

    // The head itself.
    storage::Entry numberEntry;
    numberEntry.set(std::to_string(targetNumber));
    co_await storage2::writeOne(batch,
        executor_v1::StateKey{ledger::SYS_CURRENT_STATE, ledger::SYS_KEY_CURRENT_NUMBER},
        std::move(numberEntry));

    co_await globalStateStorage.mergeToBackends(batch);
    co_return RollbackResult{.oldHead = head, .newHead = targetNumber};
}

}  // namespace bcos::scheduler_v1
