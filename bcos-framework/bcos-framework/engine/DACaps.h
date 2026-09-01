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
 * @brief DA size caps shared by miner_setMaxDASize and OP payload build.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

namespace bcos::engine
{

/// EIP-2718 envelope-byte caps from miner_setMaxDASize. Zero = uncapped.
/// maxTxSize drops an oversized sealed tx from the build; maxBlockSize stops
/// appending sealed txs once the cumulative size (forced envelopes included)
/// crosses the cap. Forced envelopes are never dropped.
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

    /// Running build budget: start with forced-envelope bytes, then admits() each sealed tx.
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
