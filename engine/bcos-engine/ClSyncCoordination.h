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
 * @file ClSyncCoordination.h
 * @brief Shared CL-driving state between the Engine API service and the EL-mode
 *        devp2p sync loop (the backfiller)
 */

#pragma once

#include <bcos-utilities/FixedBytes.h>

#include <atomic>
#include <mutex>
#include <optional>

namespace bcos::engine::engine_common
{
/// CL-driven sync coordination, shared between the Engine API service (the CL-facing
/// lane) and the EL-mode devp2p sync loop (the backfiller). Created by the
/// composition root only on the [engine_rpc] EL wiring — "config is intent": with the
/// authenticated Engine API listener enabled, a CL is expected to drive the chain, so
/// the sync loop's autonomous advance is demoted to a bootstrap that runs only until
/// the first forkchoiceUpdated arrives; without it, the loop stays fully autonomous
/// and no instance of this class exists.
///
/// Two pieces of state cross the lane boundary:
///   * the CL-driven latch — set by the FIRST served forkchoiceUpdated and never
///     cleared, so a CL disconnect cannot revive autonomous advance and commit past
///     the head a returning CL expects;
///   * the backfill target — the latest block hash the CL referenced but this node
///     has not committed (every Engine SYNCING answer records one). Latest wins: a
///     newer FCU/newPayload supersedes a stale target.
class ClSyncCoordination
{
public:
    /// Engine side: latch CL-driven mode. Called on every served forkchoiceUpdated;
    /// cheap and idempotent.
    void noteForkchoiceServed() noexcept { m_clDriving.store(true, std::memory_order_relaxed); }

    /// Engine side: the CL named a block hash this node lacks. The sync loop picks
    /// the target up on its next round and backfills toward it.
    void requestBackfill(h256 const& targetHash)
    {
        std::lock_guard const lock(m_mutex);
        m_backfillTarget = targetHash;
    }

    /// Sync-loop side.
    bool clDriving() const noexcept { return m_clDriving.load(std::memory_order_relaxed); }

    /// Sync-loop side: the pending backfill target, if any.
    std::optional<h256> backfillTarget() const
    {
        std::lock_guard const lock(m_mutex);
        return m_backfillTarget;
    }

    /// Sync-loop side: the target was served (the backfill reached it, or it turned
    /// out already committed) — or can never be served by download (it names a
    /// non-canonical block at/below the committed tip: a side fork, which needs the
    /// rollback lane, not more download). Conditional on the target still being
    /// @p servedHash, so a newer request arriving mid-backfill is never cleared.
    /// A cleared unresolvable target re-arms on the CL's next SYNCING answer.
    void clearBackfillTarget(h256 const& servedHash)
    {
        std::lock_guard const lock(m_mutex);
        if (m_backfillTarget == servedHash)
        {
            m_backfillTarget.reset();
        }
    }

private:
    std::atomic<bool> m_clDriving{false};
    mutable std::mutex m_mutex;
    std::optional<h256> m_backfillTarget;
};
}  // namespace bcos::engine::engine_common
