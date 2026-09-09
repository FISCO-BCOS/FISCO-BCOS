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
#include "HistoryIndex.h"
#include "MPTHistory.h"
#include "ReverseHistoryStore.h"
#include <bcos-framework/storage2/AnyStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <concepts>
#include <memory>
#include <optional>
#include <range/v3/range/concepts.hpp>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace bcos::ledger::mpt::history
{

/// What the flat state row @p key held at block @p block (spec §10.1).
///
/// A thin adapter over StateHistoryStore::readAt whose only job is the key mapping, so that the
/// physical form a row is recorded under is written down once (historyKeyOf) and both the capture
/// and the query go through it.
///
/// Returns the raw three-way answer, HistoryUseCurrent included — so a caller that acts on that
/// arm must go through readStateAtOrCurrent below instead, which is the only place allowed to
/// turn "unchanged since B" into an actual value.
///
/// @param store the node's ONE state-history store — the object that owns the in-memory index the
///        lookup runs against (MPTHistory::state()). Passing the store rather than constructing
///        one is not a style choice: a second instance would have its own empty index and would
///        report "this key never changed" for every block, which reads as today's value under an
///        old block's number (G10).
/// @param backend the committed plane the shard rows live in.
/// @param tip the chain's committed tip at the time of the query, and @p depth this node's
///        H_state: together they are the window guard that runs BEFORE the lookup (G5, spec B.3).
/// @throws HistoryPruned when @p block predates the retained window — the caller must surface
///         that, never fall back to the current value (G6).
/// @throws HistoryIndexUnavailable when the index was never rebuilt or a rebuild failed.
template <ReadableStateStorage Backend>
[[nodiscard]] task::Task<ReadAtResult> readStateAt(StateHistoryStore const& store, Backend& backend,
    executor_v1::StateKeyView const& key, protocol::BlockNumber block, protocol::BlockNumber tip,
    protocol::BlockNumber depth)
{
    executor_v1::StateKey const rowKey{key};
    co_return co_await store.readAt(backend, historyKeyOf(rowKey), block, tip, depth);
}

namespace detail
{
/// A recorded pre-image is `bcos::bytes`; the plane it is being returned alongside speaks either
/// `bcos::bytes` (trie nodes) or `storage::Entry` (flat state rows). One conversion, chosen at
/// compile time, so readAtOrCurrent can hand back the same type on both of its arms.
template <class Value>
Value valueFromRecorded(bcos::bytes&& recorded)
{
    if constexpr (std::constructible_from<Value, bcos::bytes&&>)
    {
        return Value(std::move(recorded));
    }
    else
    {
        return Value(
            std::string_view{reinterpret_cast<char const*>(recorded.data()), recorded.size()});
    }
}
}  // namespace detail

/// One historical read, end to end: the recorded pre-image if there is one, the current value if
/// the key has not changed since — and never the current value when a commit could have moved it
/// underneath the query.
///
/// This is the ONE place allowed to act on `HistoryUseCurrent`, and every caller goes through it
/// (EthEndpoint's historicalStateRow, HistoricalStateBackend::resolveHistoricalRow,
/// HistoricalNodeStorage::readOne). The reason is a race that no lock inside the index can close:
///
///   the commit path merges block N's rows to disk, and only afterwards publishes N to the index.
///   In between — and again for the instant between a reader's `locate` and its own read of the
///   current value — the disk holds N's new value while the index does not know N exists. A query
///   for any B < N-1 passes admission, misses the index, reads the current value and gets N's
///   bytes labelled B.
///
/// The generation counter is what makes that observable: readAt returns the (even) generation it
/// resolved "unchanged" at, and this function re-reads it after the current-value read. Equal
/// means nothing published in between and the value really is block B's. Different, or odd, means
/// the read may be from a plane that is ahead of the index, so the whole thing is recomputed —
/// and after a bounded number of attempts refused, because answering is the one thing that must
/// not happen.
///
/// @param currentReader an awaitable-returning callable with no arguments that reads the key's
///        value from the CURRENT committed plane and yields `std::optional<Value>`. It is invoked
///        only on the UseCurrent arm, so a query whose key does have a recorded pre-image never
///        touches the current plane at all. Its `Value` is what this function returns, and it is
///        built from the recorded bytes on the other arm.
/// @throws whatever readAt throws, plus HistoryIndexUnavailable when the retry budget is spent.
template <class Store, class Backend, class CurrentReader>
[[nodiscard]] auto readAtOrCurrent(Store const& store, Backend& backend,
    std::span<const bcos::byte> key, protocol::BlockNumber block, protocol::BlockNumber tip,
    protocol::BlockNumber depth, CurrentReader&& currentReader)
    -> task::Task<task::AwaitableReturnType<std::invoke_result_t<CurrentReader&>>>
{
    using CurrentResult = task::AwaitableReturnType<std::invoke_result_t<CurrentReader&>>;
    using Value = typename CurrentResult::value_type;
    // The same budget readAt uses for the window itself: enough that only a commit which died
    // mid-publish can exhaust it.
    constexpr std::size_t kGenerationRetryBudget = 4096;

    for (std::size_t attempt = 0;; ++attempt)
    {
        auto version = co_await store.readAt(backend, key, block, tip, depth);
        if (auto* recorded = std::get_if<bcos::bytes>(std::addressof(version)))
        {
            co_return CurrentResult{detail::valueFromRecorded<Value>(std::move(*recorded))};
        }
        if (std::holds_alternative<HistoryAbsent>(version))
        {
            co_return std::nullopt;
        }

        auto const& useCurrent = std::get<HistoryUseCurrent>(version);
        auto current = co_await currentReader();
        // The whole point: the value above was read from a plane the index does not control, so
        // it is only block @p block's value if no commit published while it was being read.
        auto const after = store.index().generation();
        if (after == useCurrent.generation && (after % 2) == 0)
        {
            co_return current;
        }
        if (attempt >= kGenerationRetryBudget)
        {
            BOOST_THROW_EXCEPTION(
                HistoryIndexUnavailable() << bcos::errinfo_comment(
                    "commits kept publishing while this historical read was resolving an "
                    "unchanged-since value; refusing rather than answering from a plane that may "
                    "be ahead of the index"));
        }
        std::this_thread::yield();
    }
}

/// What the flat state row @p key held at block @p block, current-value arm included and proved
/// (readAtOrCurrent). This is what the state-side callers use; `readStateAt` above is the raw
/// three-way answer, for callers that need to distinguish the arms themselves.
///
/// The current value is read from @p backend, which is the same committed plane the window
/// guard's @p tip describes — a pending, not-yet-committed block must not leak into a historical
/// answer, and a committed one must not either unless the index already knows about it.
template <ReadableStateStorage Backend>
[[nodiscard]] task::Task<std::optional<executor_v1::StateValue>> readStateAtOrCurrent(
    StateHistoryStore const& store, Backend& backend, executor_v1::StateKeyView const& key,
    protocol::BlockNumber block, protocol::BlockNumber tip, protocol::BlockNumber depth)
{
    executor_v1::StateKey const rowKey{key};
    co_return co_await readAtOrCurrent(store, backend, historyKeyOf(rowKey), block, tip, depth,
        [&backend, &rowKey]() { return storage2::readOne(backend, rowKey); });
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

/// Build the backend handle MPTHistory takes: an AnyStorage that OWNS its
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

/// Can a query at block @p block be answered at all from @p store's rows?
///
/// Pure memory under the optimized layout: everything it asks is in the in-memory index, so the
/// admission check that used to cost two point reads and a seek per query now costs three map
/// probes. No I/O also means no coroutine — callers get a plain bool.
///
/// Four things have to hold, and they fail differently:
///
///  1. the index is entitled to answer at all. `Unavailable` (a rebuild or a publish failed) is a
///     statement that the store does not know what it holds, and answering "not covered" would
///     let a caller present that as a normal "this node retains nothing" — so it THROWS
///     HistoryIndexUnavailable and the RPC maps it to its own code. `Empty` (never rebuilt, or a
///     store whose depth is 0) is the ordinary "nothing here" and returns false (G10);
///  2. the store has recorded SOMETHING — its retention boundary is known. A node that never
///     wrote history (the depth was 0, the MPT was enabled later, the era predates this feature)
///     has no boundary, and every key would otherwise miss the index and report
///     HistoryUseCurrent, i.e. hand back today's state wearing block B's label. -> false;
///  3. @p block is not already below the boundary. -> HistoryPruned;
///  4. block B+1 recorded its diff — the reverse history answers "the value at B" from the FIRST
///     change after B, so that block's records are the necessary condition. On a scenario-A chain
///     this is what rules out every pre-activation height. -> false.
///
/// Call it ONCE per query, before the per-key reads. It is an ADMISSION check and nothing more:
/// it says this node recorded the era and still retains it, at the instant it is asked. Two other
/// mechanisms cover what happens afterwards, and neither is replaceable by re-running this one:
///
///  - an expiry landing mid-query moves the index boundary under the same lock readAt's `locate`
///    takes, so a version located after it is either still there or reported as HistoryPruned by
///    readAt itself;
///  - a COMMIT landing mid-query would leave the disk ahead of the index, which is what the
///    publish generation and readAtOrCurrent above exist for — this check cannot see it, because
///    the wrong value would come from the current plane rather than from the index.
///
/// A hole in the MIDDLE of the window is invisible to all of them — spec §13 assigns range
/// continuity to the audit, and that is where a complete answer belongs.
///
/// @throws HistoryPruned when @p block is below the store's retention boundary.
/// @throws HistoryIndexUnavailable when the store's index is unusable.
template <class Store>
[[nodiscard]] bool historyCoversBlock(
    Store const& store, protocol::BlockNumber block, protocol::BlockNumber tip)
{
    auto const& index = store.index();
    switch (index.state())
    {
    case IndexState::Unavailable:
        BOOST_THROW_EXCEPTION(
            HistoryIndexUnavailable() << bcos::errinfo_comment(
                "the reverse-history query index is unusable; historical reads are refused until "
                "this node is restarted with a successful rebuild"));
    case IndexState::Empty:
        return false;
    case IndexState::Ready:
        break;
    }
    if (block >= tip)
    {
        // The tip's rows ARE the current state, so no history is consulted for it — but the state
        // check above still runs first, and deliberately: an Unavailable index means this node
        // does not know what it holds, and answering "covered" would let a caller present a
        // degraded node as a healthy one. It costs a tip query on a broken node a refusal it does
        // not strictly need, and that refusal disappears at the first successful rebuild.
        return true;
    }
    auto const boundary = index.boundary();
    if (!boundary)
    {
        return false;
    }
    if (*boundary > block)
    {
        BOOST_THROW_EXCEPTION(HistoryPruned() << bcos::errinfo_comment(
                                  "requested block is below this store's retention boundary"));
    }
    return store.recordedBlock(block + 1);
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
    /// @param store the node's ONE trie-history store (MPTHistory::trie()) — it owns the index
    ///        every position is located through.
    /// @param backend the committed plane the TrieHistory shard rows live in.
    /// @param block the height being proved, @p tip the committed tip, @p depth this node's
    ///        H_proof.
    HistoricalNodeStorage(NodeStorage& current, TrieHistoryStore const& store,
        HistoryBackend& backend, protocol::BlockNumber block, protocol::BlockNumber tip,
        protocol::BlockNumber depth)
      : m_current(std::addressof(current)),
        m_store(std::addressof(store)),
        m_backend(std::addressof(backend)),
        m_block(block),
        m_tip(tip),
        m_depth(depth)
    {}

    task::Task<std::optional<bcos::bytes>> readOne(PathKey const& key)
    {
        executor_v1::StateKey const rowKey = pathNodeStateKey(key);
        // Through readAtOrCurrent, not readAt: the "this position has not changed since B" arm
        // ends in a read of the CURRENT node row, and that read has to be proved not to have
        // crossed a commit (see readAtOrCurrent).
        co_return co_await readAtOrCurrent(*m_store, *m_backend, historyKeyOf(rowKey), m_block,
            m_tip, m_depth, [this, &key]() { return bcos::storage2::readOne(*m_current, key); });
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
    TrieHistoryStore const* m_store;
    HistoryBackend* m_backend;
    protocol::BlockNumber m_block;
    protocol::BlockNumber m_tip;
    protocol::BlockNumber m_depth;
};

}  // namespace bcos::ledger::mpt::history
