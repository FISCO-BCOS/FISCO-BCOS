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
 * @brief per-block counters behind a process-wide switch (log.enable_block_stat)
 * @file BlockStat.h
 */
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace bcos
{
/// Process-wide switch. Set once from log.enable_block_stat in BoostLogInitializer::initLog.
/// While disabled, BlockStatCounters::add is a single relaxed load and returns.
class BlockStat
{
public:
    static void enable();
    static void disable();
    static bool enabled();
};

/// Fixed-slot counters. Each module defines its own slot enum and owns one instance; the module
/// reads every slot with takeAndReset at its "block N committed" point and prints one
/// [MODULE][METRIC]BlockStat line. bcos-utilities does not know the modules.
template <size_t N>
class BlockStatCounters
{
public:
    void add(size_t _slot, uint64_t _n = 1)
    {
        if (!BlockStat::enabled())
        {
            return;
        }
        m_slots[_slot].fetch_add(_n, std::memory_order_relaxed);
    }

    uint64_t takeAndReset(size_t _slot)
    {
        return m_slots[_slot].exchange(0, std::memory_order_relaxed);
    }

    uint64_t peek(size_t _slot) const { return m_slots[_slot].load(std::memory_order_relaxed); }

    static constexpr size_t size() { return N; }

private:
    std::array<std::atomic<uint64_t>, N> m_slots{};
};
}  // namespace bcos
