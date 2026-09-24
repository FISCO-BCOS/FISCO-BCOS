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
 * @file OpForkId.h
 * @brief EIP-2124 fork-id for OP-Stack chains (the opstack-el sync handshake).
 *
 * Sourced from op-geth (optimism branch):
 *  - core/forkid/forkid.go gatherForks collects fork points by REFLECTION over
 *    params.ChainConfig: every *uint64 field whose name ends in "Time" (and every
 *    *big.Int field ending in "Block"), sorted, deduplicated, with time forks
 *    <= genesis time and block forks at block 0 dropped.
 *  - params/superchain.go LoadOPStackChainConfig fills that struct for an OP
 *    chain: ShanghaiTime=CanyonTime, CancunTime=EcotoneTime, PragueTime=IsthmusTime
 *    (same value twice -> one fork-id point after dedup), plus the OP-named
 *    Canyon/Ecotone/Fjord/Granite/Holocene/Isthmus/Jovian/Karst/Lagoon times.
 *    RegolithTime is always 0 (dropped: <= genesis). Delta has NO ChainConfig
 *    field ("the Delta upgrade does not affect the execution-layer"), so it is
 *    NOT a fork-id point. OsakaTime stays nil on OP chains (Karst is its own
 *    field), and every block-based fork is 0 (Bedrock genesis) and dropped.
 *
 * The OP fork-id ladder is therefore exactly:
 *   canyon, ecotone, fjord, granite, holocene, isthmus, jovian, karst
 * (Lagoon would append after Karst once scheduled; the ten-key
 * [op_fork_timestamps] schedule ends at Karst, so a chain that schedules Lagoon
 * in op-geth is ahead of this ladder — peers then reject our checksum past
 * Karst's activation, which is the EIP-2124 fail-closed behaviour.)
 * @date 2026/9/22
 */
#pragma once

#include "ForkId.h"
#include <bcos-utilities/FixedBytes.h>
#include <array>
#include <cstdint>
#include <limits>

namespace bcos::devp2p::eth
{
/// The OP fork-id time ladder in activation order (see the file comment for the op-geth
/// derivation). UINT64_MAX encodes op-node's nil ("not scheduled", the GenesisConfig /
/// OpChainConfig default for an absent key) and is DROPPED, not terminal: op-geth's
/// gatherForks simply never collects a nil field, so an unscheduled intermediate rung
/// (e.g. the isthmus-baseline shape, where only jovian/karst are set) must not end the
/// ladder. Forks at or below the genesis timestamp, and exact duplicates (a chain that
/// activates two forks in one block), are skipped by forkIdFromTimeLadder — matching
/// gatherForks' genesis filter and dedup.
inline constexpr size_t c_opForkIdLadderSize = 8;
using OpForkIdLadder = std::array<uint64_t, c_opForkIdLadderSize>;  // canyon..karst

/// EIP-2124 fork-id for an OP chain, mirroring geth's forkid.NewID(config, genesis,
/// head.Number, head.Time): the checksum chains the genesis hash with every ladder
/// fork the LOCAL head has already passed (crc32 over the 8-byte big-endian fork
/// value, geth's checksumUpdate); next is the first scheduled fork not yet passed
/// (0 when none). Reflects the local head, NOT wall-clock time — a fresh node
/// announces {crc32(genesisHash), firstFork}, which every remote accepts via
/// EIP-2124 rule #2.
inline ForkId computeOpForkId(bcos::h256 const& _genesisHash, uint64_t _genesisTime,
    uint64_t _localHeadTime, OpForkIdLadder const& _ladder)
{
    uint32_t const checksum =
        crc32(bcos::bytesConstRef(_genesisHash.data(), _genesisHash.size()));
    // Compact out the unscheduled (UINT64_MAX) rungs before handing the ladder to the
    // generic walker: its UINT64_MAX-is-terminal rule exists for the L1 tail, where an
    // unscheduled fork can only ever be the LAST known one. The OP schedule allows
    // unscheduled intermediate rungs, so they are filtered here instead.
    OpForkIdLadder scheduled{};
    size_t count = 0;
    for (uint64_t fork : _ladder)
    {
        if (fork != std::numeric_limits<uint64_t>::max())
        {
            scheduled[count++] = fork;
        }
    }
    return forkIdFromTimeLadder(checksum, _genesisTime, _localHeadTime,
        std::span<const uint64_t>(scheduled.data(), count));
}
}  // namespace bcos::devp2p::eth
