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
 * @file HistoryRead.h
 * @brief The query half of the two reverse histories: one flat row as of block B (spec §10.1) and
 *        a whole trie-node plane as of block B (spec §10.2)
 */
#pragma once

#include "../PathKey.h"
#include "HistoryCommit.h"
#include "HistoryErrors.h"
#include "ReverseHistoryStore.h"
#include <bcos-framework/storage2/AnyStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <memory>
#include <optional>
#include <range/v3/range/concepts.hpp>
#include <stdexcept>
#include <utility>
#include <vector>

namespace bcos::ledger::mpt::history
{

/// What the flat state row @p key held at block @p block (spec §10.1).
///
/// A thin adapter over StateHistoryStore::readAt whose only job is the key mapping, so that the
/// physical form a row is indexed under is written down once (historyKeyOf) and both the capture
/// and the query go through it.
///
/// @param backend the committed, seekable plane the history rows live in.
/// @param tip the chain's committed tip at the time of the query, and @p depth this node's
///        H_state: together they are the window guard that runs BEFORE the seek (G5, spec B.3).
/// @throws HistoryPruned when @p block predates the retained window — the caller must surface
///         that, never fall back to the current value (G6).
template <QueryableStateStorage Backend>
[[nodiscard]] task::Task<ReadAtResult> readStateAt(Backend& backend,
    executor_v1::StateKeyView const& key, protocol::BlockNumber block, protocol::BlockNumber tip,
    protocol::BlockNumber depth)
{
    executor_v1::StateKey const rowKey{key};
    co_return co_await StateHistoryStore::readAt(backend, historyKeyOf(rowKey), block, tip, depth);
}

/// Read-only façade over the plane the history rows live in, so it can be type-erased.
///
/// It exists for one mechanical reason: `AnyStorage`'s erasure needs a `removeSome(keys, tag)`
/// overload, which `RocksDBStorage2` — the production plane — does not have, so the backend
/// cannot be erased directly. The guard rails are the point rather than a side effect: a handle
/// handed to the RPC layer must not be able to write to the committed state.
///
/// Reads and ranges (RANGE_SEEK included) forward verbatim.
template <class Storage>
class HistoryReadStorage
{
public:
    using Key = executor_v1::StateKey;
    using Value = executor_v1::StateValue;

    explicit HistoryReadStorage(Storage& storage) noexcept : m_storage(std::addressof(storage)) {}

    auto readOne(auto&& key) { return m_storage->readOne(std::forward<decltype(key)>(key)); }
    auto readSome(::ranges::input_range auto keys) { return m_storage->readSome(std::move(keys)); }
    auto range(auto&&... args) { return m_storage->range(std::forward<decltype(args)>(args)...); }

    task::Task<void> writeOne(Key /*key*/, Value /*value*/)
    {
        throwReadOnly();
        co_return;
    }
    task::Task<void> writeSome(::ranges::input_range auto /*keyValues*/)
    {
        throwReadOnly();
        co_return;
    }
    task::Task<void> removeOne(Key /*key*/, auto&&... /*tags*/)
    {
        throwReadOnly();
        co_return;
    }
    task::Task<void> removeSome(::ranges::input_range auto /*keys*/, auto&&... /*tags*/)
    {
        throwReadOnly();
        co_return;
    }

private:
    [[noreturn]] static void throwReadOnly()
    {
        BOOST_THROW_EXCEPTION(std::logic_error(
            "HistoryReadStorage is a read-only view over the committed MPT history rows"));
    }

    Storage* m_storage;
};

/// Build the handle NodeService::setMPTHistoryReader takes: an AnyStorage that OWNS its
/// read-only adapter through an aliasing shared_ptr, so callers manage exactly one lifetime.
/// Only @p storage itself is borrowed (production: the committed state backend, owned by the
/// Initializer), and it must outlive the returned handle.
template <class Storage>
[[nodiscard]] std::shared_ptr<
    bcos::storage2::AnyStorage<executor_v1::StateKey, executor_v1::StateValue>>
makeHistoryReader(Storage& storage)
{
    using AnyHistoryStorage =
        bcos::storage2::AnyStorage<executor_v1::StateKey, executor_v1::StateValue>;
    struct OwningReader
    {
        HistoryReadStorage<Storage> adapter;
        std::optional<AnyHistoryStorage> erased;

        explicit OwningReader(Storage& storage) : adapter(storage) { erased.emplace(adapter); }
    };
    auto owner = std::make_shared<OwningReader>(storage);
    return {owner, std::addressof(*owner->erased)};
}

/// Can a query at block @p block be answered at all from @p Store's rows?
///
/// Three things have to hold, and they fail differently:
///
///  1. the store has recorded SOMETHING — its retention-boundary row exists. A node that never
///     wrote history (the depth was 0, the MPT was enabled later, the era predates this feature)
///     has none, and every key would otherwise seek past the end of the index and report
///     HistoryUseCurrent, i.e. hand back today's state wearing block B's label. -> false;
///  2. @p block is not already below the boundary. -> HistoryPruned;
///  3. block B+1 recorded its diff — the reverse history answers "the value at B" from the FIRST
///     change after B, so that block's manifest is the necessary condition. On a scenario-A
///     chain this is what rules out every pre-activation height. -> false.
///
/// Then the boundary is re-read, for the same reason readAt re-reads it: an expiry that lands
/// between the checks above and the per-key seeks that follow would leave the query answering
/// from a window it no longer has. The boundary advances in the same Write as the deletes, so
/// this ordering catches it (spec B.3, §13).
///
/// Call it ONCE per query, before the per-key reads; readAt's own guard is the other half and
/// neither replaces the other. A hole in the MIDDLE of the window is invisible to both — spec
/// §13 assigns range continuity to the audit, and that is where a complete answer belongs.
///
/// @throws HistoryPruned when @p block is below the store's retention boundary.
template <class Store, QueryableStateStorage Backend>
[[nodiscard]] task::Task<bool> historyCoversBlock(
    Backend& backend, protocol::BlockNumber block, protocol::BlockNumber tip)
{
    if (block >= tip)
    {
        // The tip needs no history: its rows ARE the current state.
        co_return true;
    }
    auto const boundary = co_await Store::retentionBoundary(backend);
    if (!boundary)
    {
        co_return false;
    }
    if (*boundary > block)
    {
        BOOST_THROW_EXCEPTION(HistoryPruned() << bcos::errinfo_comment(
                                  "requested block is below this store's retention boundary"));
    }
    if (!co_await Store::recordedBlock(backend, block + 1))
    {
        co_return false;
    }
    auto const boundaryAfter = co_await Store::retentionBoundary(backend);
    if (!boundaryAfter || *boundaryAfter > block)
    {
        BOOST_THROW_EXCEPTION(
            HistoryPruned() << bcos::errinfo_comment(
                "the retained history window moved past the requested block while the query was "
                "being admitted"));
    }
    co_return true;
}

/// A read-only PathKey -> RLP storage that answers with each position's version at block B
/// (spec §10.2, step 3).
///
/// This is the whole of "historical proof": every trie reader in the module already locates nodes
/// BY POSITION and verifies them against the hash the parent recorded, so swapping the plane the
/// positions are read from is enough. `Trie`, `MPTReadView`, `holdsTrieRoot`, `proofWalk` and
/// `generateProof` are unchanged and simply instantiate over this instead of over the current node
/// storage; the parent-child hash check then proves the historical bytes just as it proves the
/// current ones — a wrong version fails it, which is precisely why the index may be trusted.
///
/// Per position: `HistoryPruned` (out of window) propagates to the caller; a recorded value IS the
/// version at B; `HistoryAbsent` means nothing occupied that position at B, which reads as a
/// missing row and lets the walk report a dead end; `HistoryUseCurrent` means the position has not
/// changed since B, so the current row IS the historical one.
///
/// NOT thread-safe and not a general storage: writes, deletes and ranges throw. One instance
/// serves one query.
template <class NodeStorage, class HistoryBackend>
    requires QueryableStateStorage<HistoryBackend>
class HistoricalNodeStorage
{
public:
    using Key = PathKey;
    using Value = bcos::bytes;

    /// @param current the node rows as they stand now (a committed-plane reader).
    /// @param backend the committed, seekable plane the TrieHistory rows live in.
    /// @param block the height being proved, @p tip the committed tip, @p depth this node's
    ///        H_proof.
    HistoricalNodeStorage(NodeStorage& current, HistoryBackend& backend,
        protocol::BlockNumber block, protocol::BlockNumber tip, protocol::BlockNumber depth)
      : m_current(std::addressof(current)),
        m_backend(std::addressof(backend)),
        m_block(block),
        m_tip(tip),
        m_depth(depth)
    {}

    task::Task<std::optional<bcos::bytes>> readOne(PathKey const& key)
    {
        executor_v1::StateKey const rowKey = pathNodeStateKey(key);
        auto version = co_await TrieHistoryStore::readAt(
            *m_backend, historyKeyOf(rowKey), m_block, m_tip, m_depth);
        if (auto* recorded = std::get_if<bcos::bytes>(std::addressof(version)))
        {
            co_return std::move(*recorded);
        }
        if (std::holds_alternative<HistoryAbsent>(version))
        {
            co_return std::nullopt;
        }
        co_return co_await bcos::storage2::readOne(*m_current, key);
    }

    task::Task<std::vector<std::optional<bcos::bytes>>> readSome(::ranges::input_range auto keys)
    {
        std::vector<std::optional<bcos::bytes>> values;
        if constexpr (::ranges::sized_range<decltype(keys)>)
        {
            values.reserve(::ranges::size(keys));
        }
        for (auto const& key : keys)
        {
            values.emplace_back(co_await readOne(key));
        }
        co_return values;
    }

    // --- read-only guard rails, same shape as MPTNodeReadStorage ---

    /// The iterator type range() nominally yields; never actually produced.
    struct NoIterator
    {
        task::Task<std::optional<std::pair<PathKey, storage2::StorageValueType<bcos::bytes>>>>
        next()
        {
            throwReadOnly();
            co_return std::nullopt;
        }
    };

    task::Task<void> writeOne(Key /*key*/, bcos::bytes /*value*/)
    {
        throwReadOnly();
        co_return;
    }
    task::Task<void> writeSome(::ranges::input_range auto /*keyValues*/)
    {
        throwReadOnly();
        co_return;
    }
    task::Task<void> removeOne(Key /*key*/, auto&&... /*tags*/)
    {
        throwReadOnly();
        co_return;
    }
    task::Task<void> removeSome(::ranges::input_range auto /*keys*/, auto&&... /*tags*/)
    {
        throwReadOnly();
        co_return;
    }
    task::Task<NoIterator> range(auto&&... /*args*/)
    {
        throwReadOnly();
        co_return NoIterator{};
    }

private:
    [[noreturn]] static void throwReadOnly()
    {
        BOOST_THROW_EXCEPTION(std::logic_error(
            "HistoricalNodeStorage is a read-only view of the trie nodes at a past block"));
    }

    NodeStorage* m_current;
    HistoryBackend* m_backend;
    protocol::BlockNumber m_block;
    protocol::BlockNumber m_tip;
    protocol::BlockNumber m_depth;
};

}  // namespace bcos::ledger::mpt::history
