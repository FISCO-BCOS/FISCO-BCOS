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
#include <memory>

namespace bcos::engine
{

/// The OP Stack batcher's DA throttling handshake: it pushes
/// miner_setMaxDASize(maxCanonTxSize, maxBlockSize) on every L2 endpoint and treats a
/// missing method as fatal. The RPC layer receives the values; the engine's payload
/// build consumes them. The two layers share nothing else, so the caps live here —
/// one instance created by the initializer, handed to both sides (NodeService carries
/// it for the RPC, the engine service ctor receives it directly).
///
/// Semantics (both in BYTES of the serialized EIP-2718 envelope, matching the
/// build-path TODO's documented contract and op-geth's miner shrinking under throttle):
///   maxTxSize   — a sealed pool tx whose envelope exceeds this is dropped from the
///                 build (op-geth rejects oversized txs from blocks the same way);
///   maxBlockSize — block assembly stops appending sealed envelopes once the
///                  cumulative serialized size (forced envelopes included in the
///                  accounting, never dropped — the leading deposit is consensus-
///                  required) crosses the cap.
/// Zero means UNSET = uncapped (the atomics' zero init), so a node without throttling
/// behaves exactly as before.
struct DACaps
{
    std::atomic<std::uint64_t> maxTxSize{0};
    std::atomic<std::uint64_t> maxBlockSize{0};

    /// Envelope-size gate for sealed pool txs (0 = everything passes).
    bool txFits(std::uint64_t envelopeSize) const noexcept
    {
        auto const cap = maxTxSize.load(std::memory_order_relaxed);
        return cap == 0 || envelopeSize <= cap;
    }

    /// Running byte budget for block assembly: construct with the forced (undroppable)
    /// envelope total, then admits(sealedEnvelopeSize) per sealed tx in order. This is a
    /// plain helper, not enforced state — the build loop owns the decisions.
    class Budget
    {
    public:
        explicit Budget(DACaps const& caps, std::uint64_t forcedBytes)
          : m_caps(caps), m_used(forcedBytes)
        {}
        bool admits(std::uint64_t envelopeSize) noexcept
        {
            auto const cap = m_caps.maxBlockSize.load(std::memory_order_relaxed);
            if (cap == 0)
            {
                return true;
            }
            if (m_used + envelopeSize > cap)
            {
                return false;
            }
            m_used += envelopeSize;
            return true;
        }

    private:
        DACaps const& m_caps;
        std::uint64_t m_used;
    };
};

}  // namespace bcos::engine
