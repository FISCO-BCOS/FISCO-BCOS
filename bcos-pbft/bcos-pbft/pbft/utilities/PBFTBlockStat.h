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
 * @brief per-block PBFT counters, printed as [CONSENSUS][PBFT][METRIC]BlockStat after Report
 * @file PBFTBlockStat.h
 */
#pragma once
#include <bcos-utilities/BlockStat.h>
#include <cstddef>

namespace bcos::consensus
{
enum class PBFTStatSlot : size_t
{
    PrePrepareRecv,
    PrepareRecv,
    CommitRecv,
    CheckpointRecv,
    ViewChangeRecv,
    Rejected,
    BytesRecv,
    Count
};

class PBFTBlockStatCounters
  : public bcos::BlockStatCounters<static_cast<size_t>(PBFTStatSlot::Count)>
{
public:
    void add(PBFTStatSlot _slot, uint64_t _n = 1)
    {
        BlockStatCounters::add(static_cast<size_t>(_slot), _n);
    }
    uint64_t takeAndReset(PBFTStatSlot _slot)
    {
        return BlockStatCounters::takeAndReset(static_cast<size_t>(_slot));
    }
};
}  // namespace bcos::consensus
