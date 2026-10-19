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
 * @brief SealSkipped / SealResumed: one line per state change, shared by sealer and PBFT
 * @file SealStall.h
 */
#pragma once
#include "Common.h"
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <array>
#include <mutex>
#include <optional>
#include <string_view>

namespace bcos::consensus
{
enum class SealSkipReason : uint8_t
{
    NoTxs,
    AlreadyCommitted,
    NotLeader,
    WaitReseal,
    SysProposalPending,
    PrevExecuting,
};

inline std::string_view sealSkipReasonName(SealSkipReason _reason)
{
    switch (_reason)
    {
    case SealSkipReason::NoTxs:
        return "no_txs";
    case SealSkipReason::AlreadyCommitted:
        return "already_committed";
    case SealSkipReason::NotLeader:
        return "not_leader";
    case SealSkipReason::WaitReseal:
        return "wait_reseal";
    case SealSkipReason::SysProposalPending:
        return "sys_proposal_pending";
    case SealSkipReason::PrevExecuting:
        return "prev_executing";
    }
    return "unknown";
}

namespace detail
{
// One state per process: the sealer and PBFT hold no common object besides the process (Sealer
// only sees the consensus through ConsensusInterface), and a node process runs exactly one of each
// -- Initializer::init builds one m_pbftInitializer for its one group. The two report different
// stages of a stall, so each keeps its own last reason: interleaved reports from the sealer and
// PBFT do not defeat each other's once-per-change suppression.
enum class SealStallSource : uint8_t
{
    Sealer,
    Pbft,
};
inline SealStallSource sealStallSource(SealSkipReason _reason)
{
    return (_reason == SealSkipReason::NoTxs || _reason == SealSkipReason::AlreadyCommitted) ?
               SealStallSource::Sealer :
               SealStallSource::Pbft;
}
struct SealStallState
{
    std::mutex mutex;
    std::array<std::optional<SealSkipReason>, 2> lastReason;
};
inline SealStallState& sealStallState()
{
    static SealStallState state;
    return state;
}
}  // namespace detail

/// Prints SealSkipped only when the reason differs from the last one its source (sealer or PBFT)
/// reported. `_until` is the block index the stall waits for (-1 when not applicable).
inline void noteSealSkipped(
    SealSkipReason _reason, bcos::protocol::BlockNumber _index, bcos::protocol::BlockNumber _until)
{
    auto& state = detail::sealStallState();
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        auto& last = state.lastReason[static_cast<size_t>(detail::sealStallSource(_reason))];
        if (last == _reason)
        {
            return;
        }
        last = _reason;
    }
    PBFT_LOG(INFO) << LOG_DESC("SealSkipped") << LOG_KV("reason", sealSkipReasonName(_reason))
                   << LOG_KV("index", _index) << LOG_KV("until", _until);
}

/// Prints SealResumed once after a SealSkipped, when a proposal is actually generated.
inline void noteSealResumed(bcos::protocol::BlockNumber _index)
{
    auto& state = detail::sealStallState();
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (!state.lastReason[0] && !state.lastReason[1])
        {
            return;
        }
        state.lastReason = {};
    }
    PBFT_LOG(INFO) << LOG_DESC("SealResumed") << LOG_KV("index", _index);
}
}  // namespace bcos::consensus
