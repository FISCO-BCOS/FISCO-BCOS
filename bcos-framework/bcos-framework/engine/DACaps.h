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
 * @file DACaps.h
 * @brief Shared DA throttling caps for the OP payload build path.
 *        Zero remains uncapped until an RPC writer (miner_setMaxDASize) is added.
 */

#pragma once

#include <atomic>
#include <cstdint>

namespace bcos::engine
{

/// The OP Stack batcher's DA throttling handshake: it pushes
/// miner_setMaxDASize(maxCanonTxSize, maxBlockSize) on every L2 endpoint and treats a
/// missing method as fatal. The RPC layer receives the values; the engine's payload
/// build consumes them. The two layers share nothing else, so the caps live here —
/// one instance created by the initializer, handed to both sides (NodeService carries
/// it for the RPC, the engine service ctor receives it directly).
///
/// Status in this slice: CONSUMER-SIDE ONLY. No miner_setMaxDASize RPC writer exists yet
/// (the caps stay zero/uncapped until that producer lands), and NodeService does not yet
/// carry the instance — the handoff described above is the intended wiring, not current
/// code.
///
/// Semantics (both in ESTIMATED DA bytes — the Fjord FastLZ size estimate of the
/// serialized EIP-2718 envelope, matching op-geth's DA throttling: its txpool DA filter
/// and the LazyTransaction DABytes carrier both use RollupCostData().EstimatedDASize(),
/// never the raw envelope length. This tree's equivalent is estimatedDaSize() in
/// bcos-evm/opstack/RollupCost.h — feed that, not raw size):
///   maxTxSize   — a sealed pool tx whose estimated DA size exceeds this is dropped
///                 from the build (op-geth's txpool excludes such txs the same way);
///   maxBlockSize — block assembly stops appending sealed envelopes once the
///                  cumulative estimated DA size (forced envelopes included in the
///                  accounting, never dropped — the leading deposit is consensus-
///                  required) crosses the cap.
/// Zero means UNSET = uncapped (the atomics' zero init), so a node without throttling
/// behaves exactly as before.
struct DACaps
{
    std::atomic<std::uint64_t> maxTxSize{0};
    std::atomic<std::uint64_t> maxBlockSize{0};

    /// Estimated-DA-size gate for sealed pool txs (0 = everything passes).
    bool txFits(std::uint64_t estimatedDaSize) const noexcept
    {
        auto const cap = maxTxSize.load(std::memory_order_relaxed);
        return cap == 0 || estimatedDaSize <= cap;
    }

    /// Running estimated-DA byte budget for block assembly: construct with the forced
    /// (undroppable) envelopes' estimate, then admits(estimatedDaSize) per sealed tx in
    /// order. The block-size cap is snapshotted at construction (one consistent view
    /// for the whole build; a Budget must be confined to the single build loop that
    /// created it, and caps pushed by the RPC mid-build take effect from the next
    /// budget on). This is a plain helper, not enforced state — the build loop owns
    /// the decisions.
    class Budget
    {
    public:
        explicit Budget(DACaps const& caps, std::uint64_t forcedBytes)
          : m_maxBlockSize(caps.maxBlockSize.load(std::memory_order_relaxed)), m_used(forcedBytes)
        {}
        bool admits(std::uint64_t estimatedDaSize) noexcept
        {
            auto const cap = m_maxBlockSize;
            if (cap == 0)
            {
                return true;
            }
            // Wrap-safe form: `m_used + estimatedDaSize` could wrap only at ~2^64 bytes,
            // but the comparison is cheap to write without the addition.
            if (cap < m_used || estimatedDaSize > cap - m_used)
            {
                return false;
            }
            m_used += estimatedDaSize;
            return true;
        }

    private:
        std::uint64_t m_maxBlockSize;
        std::uint64_t m_used;
    };
};

}  // namespace bcos::engine
