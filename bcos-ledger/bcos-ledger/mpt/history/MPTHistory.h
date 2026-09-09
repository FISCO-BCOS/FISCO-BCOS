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
 * @file MPTHistory.h
 * @brief The node's two reverse-history stores, their retention depths and the plane they read,
 *        as ONE object shared by the commit path and the RPC layer (layout spec §1.6)
 */
#pragma once

#include "HistoryDepths.h"
#include "ReverseHistoryStore.h"
#include <bcos-framework/storage2/AnyStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/BoostLog.h>
#include <boost/exception/diagnostic_information.hpp>
#include <chrono>
#include <memory>
#include <utility>

namespace bcos::ledger::mpt::history
{

/// Everything a node needs to answer a historical question, in one place.
///
/// The optimized layout made each store an OBJECT holding an in-memory index (ReverseHistoryStore
/// .h), and a derived index is only correct if there is exactly ONE of it: two instances over the
/// same tables would each be rebuilt from disk, but only the one the commit path publishes into
/// would stay current, and the other would answer "this key never changed" for every block after
/// startup — the silent wrong answer G10 exists to forbid. So the stores are owned here, and the
/// scheduler and the RPC endpoints share this object rather than constructing their own.
///
/// It also carries the two things every caller needs alongside a store: the retention depths (the
/// window guard's @p depth argument) and the read-only handle on the committed plane the shard
/// rows live in. The handle is type-erased because the RPC layer must not know what a
/// RocksDBStorage2 is, and read-only because a handle reaching the RPC layer must not be able to
/// write committed state (HistoryRead.h::makeHistoryReader builds it).
class MPTHistory
{
public:
    /// The erased committed plane. StateKey-typed because history rows ARE ordinary state rows.
    using Backend = bcos::storage2::AnyStorage<executor_v1::StateKey, executor_v1::StateValue>;

    /// @param depths nodeConfig [storage] mpt_history_state_blocks / mpt_history_proof_blocks.
    /// @param backend `makeHistoryReader(latestBackend())`. May be null in tests and on a node
    ///        whose storage was never built; every read path then refuses rather than guessing.
    MPTHistory(HistoryDepths depths, std::shared_ptr<Backend> backend) noexcept
      : m_depths(depths), m_backend(std::move(backend))
    {}
    MPTHistory(const MPTHistory&) = delete;
    MPTHistory(MPTHistory&&) = delete;
    MPTHistory& operator=(const MPTHistory&) = delete;
    MPTHistory& operator=(MPTHistory&&) = delete;
    ~MPTHistory() = default;

    [[nodiscard]] StateHistoryStore& state() noexcept { return m_state; }
    [[nodiscard]] StateHistoryStore const& state() const noexcept { return m_state; }
    [[nodiscard]] TrieHistoryStore& trie() noexcept { return m_trie; }
    [[nodiscard]] TrieHistoryStore const& trie() const noexcept { return m_trie; }
    [[nodiscard]] HistoryDepths depths() const noexcept { return m_depths; }

    /// The committed plane, or nullptr when this node has none.
    [[nodiscard]] std::shared_ptr<Backend> const& backend() const noexcept { return m_backend; }

    /// Recompute both in-memory indexes from the shard rows on disk.
    ///
    /// Called ONCE, synchronously, at startup, before any scheduler or RPC object exists — which
    /// is what makes the derived index safe: there is no moment at which a query can miss a
    /// version because the walk has not reached it yet and read that miss as "unmodified"
    /// (layout spec §1.5).
    ///
    /// A store whose depth is 0 is skipped: nothing was ever recorded for it, its index stays
    /// IndexState::Empty, and every query is refused before it reaches the store anyway.
    ///
    /// A rebuild that throws is NOT fatal to the node. The store is latched Unavailable and the
    /// other one still gets its turn: a node that refuses history reads keeps sealing, executing
    /// and committing blocks, which is the difference between a degraded node and a dead one. The
    /// ERROR line is the operator's signal to investigate; the alternative — answering from a
    /// short index — is unrecoverable because it looks like success.
    template <SeekableStateStorage Storage>
    task::Task<void> rebuild(Storage& backend)
    {
        if (m_depths.state > 0)
        {
            co_await rebuildOne(m_state, backend, "state");
        }
        if (m_depths.proof > 0)
        {
            co_await rebuildOne(m_trie, backend, "trie");
        }
    }

private:
    template <class Store, SeekableStateStorage Storage>
    static task::Task<void> rebuildOne(Store& store, Storage& backend, std::string_view name)
    {
        auto const started = std::chrono::steady_clock::now();
        try
        {
            auto const report = co_await store.rebuild(backend);
            auto const elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started);
            BCOS_LOG(INFO) << LOG_BADGE("MPTHistory") << LOG_DESC("history index rebuilt")
                           << LOG_KV("store", name) << LOG_KV("blocks", report.blocks)
                           << LOG_KV("records", report.records)
                           << LOG_KV("bytesScanned", report.bytesScanned)
                           << LOG_KV("elapsedMs", elapsed.count());
        }
        catch (...)
        {
            // Catch-all rather than MPTInvariantViolation alone: the walk also allocates and
            // reads storage, and any escape leaves an index that may be missing versions.
            store.markUnavailable();
            BCOS_LOG(ERROR) << LOG_BADGE("MPTHistory")
                            << LOG_DESC(
                                   "history index rebuild failed; this store will refuse "
                                   "every historical read until the node is restarted")
                            << LOG_KV("store", name)
                            << LOG_KV("error", boost::current_exception_diagnostic_information());
        }
    }

    HistoryDepths m_depths;
    std::shared_ptr<Backend> m_backend;
    StateHistoryStore m_state;
    TrieHistoryStore m_trie;
};

}  // namespace bcos::ledger::mpt::history
