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
 */

#pragma once

#include <atomic>
#include <cstdint>

namespace bcos::engine
{

/// Shared DA size limits for OP payload building. Initializer creates one instance
/// when [op_engine_rpc] is enabled; AirNodeInitializer wires it to NodeService so
/// miner_setMaxDASize can update caps that OpEngineService reads during assembly.
///   maxTxSize    — drop sealed pool txs above this estimated DA size.
///   maxBlockSize — stop appending sealed txs once the cumulative estimate exceeds this.
/// Zero means uncapped. Sizes use the Fjord FastLZ estimate over the EIP-2718 envelope.
struct DACaps
{
    std::atomic<std::uint64_t> maxTxSize{0};
    std::atomic<std::uint64_t> maxBlockSize{0};

    /// Per-tx gate for sealed pool txs (0 passes everything).
    bool txFits(std::uint64_t estimatedDaSize) const noexcept
    {
        auto const cap = maxTxSize.load(std::memory_order_relaxed);
        return cap == 0 || estimatedDaSize <= cap;
    }

    /// Block assembly byte budget. Seed with forced (undroppable) envelope estimates.
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
