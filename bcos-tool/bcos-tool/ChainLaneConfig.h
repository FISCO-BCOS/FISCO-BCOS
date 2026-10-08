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
 * @file ChainLaneConfig.h
 * @brief Declarative lane-key rules mirrored by NodeConfig's validateL2Invariants.
 */

#pragma once

#include <bcos-framework/ledger/LedgerConfig.h>
#include <cstdint>
#include <string_view>
#include <vector>

namespace bcos::tool
{

/// The executor lane a chain runs. Derived from ONE authoritative signal — executor.version,
/// which is already genesis-frozen, already maps to a scheduler slot (LedgerConfig.h:288-296)
/// and already carries a static_assert — instead of the four signals that used to answer this
/// question independently (executor.version, feature_l2_ethereum_compat, [ethereum] mode=el,
/// the OP schedule section).
enum class ChainLane : std::uint8_t
{
    Fisco = 0,
    Eth = 1,
    Op = 2,
};

enum class KeyPresence : std::uint8_t
{
    Required,
    Forbidden,
    Optional,
};

/// One config.genesis key's contract on one lane. `pinned` is the mechanical guard for the rule
/// that matters most: a key two nodes can disagree about and that changes execution MUST reach
/// generateGenesisData, or node admission cannot catch the disagreement (the P0 defect was
/// exactly a chain-level parameter that was in neither the config nor the pin).
struct LaneKeyRule
{
    std::string_view section;
    ChainLane lane;
    KeyPresence presence;
    bool pinned;
    /// Thrown when the section is present on a lane that forbids it. The schedule family keeps
    /// the combined two-key message the existing tests pin (NodeConfigOpForkTimestampsTest).
    std::string_view rejectMessage;
    /// Where the current behaviour was reverse-engineered from.
    std::string_view reason;
};

[[nodiscard]] inline ChainLane laneForExecutorVersion(int executorVersion) noexcept
{
    if (ledger::isOpLaneVersion(executorVersion))
    {
        return ChainLane::Op;
    }
    if (executorVersion >= ledger::ETHEREUM_EXECUTOR_VERSION)
    {
        return ChainLane::Eth;
    }
    return ChainLane::Fisco;
}

/// The OP section family, moved here from the hand-written if-pairs in
/// NodeConfig::validateL2Invariants. TODO (deliberately NOT in this change): the
/// remaining pairs —
/// [eth_genesis_header] <-> feature_l2_ethereum_compat, [alloc.*] <-> the same feature,
/// [fork_timestamps] <-> [ethereum] mode=el, mode=el -> [web3] chain_id, OP lane forbids
/// executor.evm_revision, config.ini <-> genesis EL mode — still live where they were.
[[nodiscard]] inline std::vector<LaneKeyRule> const& laneKeyRules()
{
    static std::vector<LaneKeyRule> const rules{
        {.section = "op_fork_schedule",
            .lane = ChainLane::Op,
            .presence = KeyPresence::Optional,
            .pinned = false,
            .rejectMessage =
                "[op_fork_timestamps]/[op_fork_schedule] requires executor.version >= 3 (OP lane)",
            .reason = "NodeConfig.cpp loadOpForkSchedule + validateL2Invariants"},
        {.section = "op_fork_timestamps",
            .lane = ChainLane::Op,
            .presence = KeyPresence::Required,
            .pinned = true,
            .rejectMessage =
                "[op_fork_timestamps]/[op_fork_schedule] requires executor.version >= 3 (OP lane)",
            .reason = "NodeConfig.cpp loadOpForkTimestamps + validateL2Invariants"},
        {.section = "op_eip1559",
            .lane = ChainLane::Op,
            .presence = KeyPresence::Optional,
            .pinned = true,
            .rejectMessage = "[op_eip1559] requires executor.version >= 3 (OP lane)",
            .reason = "NodeConfig.cpp loadOpEip1559 + validateL2Invariants"},
    };
    return rules;
}

}  // namespace bcos::tool
