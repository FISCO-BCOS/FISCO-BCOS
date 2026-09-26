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
 * @file OpForkSchedule.h
 * @brief The single OP-Stack fork-activation parser: the OpFork ladder enum, the
 *        [op_fork_timestamps] schedule struct and resolveOpFork, the one function every
 *        component (executor, header validator, engine) uses to decide which OP fork a
 *        block timestamp runs under. Header-only and free of bcos-tool includes so
 *        bcos-devp2p can use it (same pattern as engine/OpBaseFee.h); GenesisConfig.h
 *        re-exports the struct for the config/genesis side.
 * @date 2026/9/22
 */
#pragma once

#include <cstdint>
#include <limits>

namespace bcos::ledger
{
// ────────────────────────────────────────────────────────────────────────────
// OP-Stack fork schedule (Bedrock onward) ↔ Ethereum base fork
//
// Reference: op-geth v1.101702.2 (authority) + optimism docs / specs.
// The FULL Bedrock..Karst ladder is modeled so opstack-executor + devp2p can
// replay an OP chain (e.g. op-sepolia) from genesis. op-sepolia is post-merge
// (the Merge happened at genesis), so Bedrock/Regolith map to a Paris EVM —
// never London.
//
//   OP fork      | Ethereum base | EVM rev (FB)
//   -------------+---------------+-----------------------------------
//   Bedrock      | Paris (merge@genesis) | EVMC_PARIS   (genesis fork, legacy L1 fee)
//   Regolith     | Paris         | EVMC_PARIS   (deposit-tx fixes)
//   Canyon       | Shanghai      | EVMC_SHANGHAI (EIP-4895/1153/5656/6780, deposit receipt version)
//   Delta        | Shanghai      | EVMC_SHANGHAI (no EL change)
//   Ecotone      | Cancun        | EVMC_CANCUN  (blob L1 fee, EIP-4844/4788/7516)
//   Fjord        | Cancun        | EVMC_CANCUN  (FastLZ L1 fee, p256 active)
//   Granite      | Cancun        | EVMC_CANCUN  (8 precompile size limits)
//   Holocene     | Cancun        | EVMC_CANCUN  (EIP-1559 via 9B extraData)
//   Isthmus      | Prague/Pectra | EVMC_PRAGUE  (EIP-7702/7623/2935/2537 + OP deposit changes)
//   Jovian       | Prague        | EVMC_PRAGUE  (+DA footprint, operator fee ×100)
//   Karst        | Osaka (Fusaka EL half) | EVMC_OSAKA (Jovian fees + Osaka EVM)
//
// The enum ORDER is the activation order and carries comparison semantics:
// `fork >= OpFork::Canyon` is how fork-gated rules are expressed (header
// validator, executor, engine). Never reorder or renumber.
// ────────────────────────────────────────────────────────────────────────────
enum class OpFork
{
    Bedrock,
    Regolith,
    Canyon,
    Delta,
    Ecotone,
    Fjord,
    Granite,
    Holocene,
    Isthmus,
    Jovian,
    Karst,
};

// OP-lane fork schedule, parsed from the [op_fork_timestamps] section of
// config.genesis (executor_version >= OPSTACK_EXECUTOR_VERSION). OP forks
// activate by L2 block TIMESTAMP IN SECONDS, exactly like op-node's
// rollup.json *_time fields (op-node/rollup/types.go:
// IsJovian(ts) == Time != nil && ts >= *Time). 0 means "active from genesis";
// std::numeric_limits<uint64_t>::max() encodes op-node's nil, i.e. "not
// scheduled". Bedrock is the genesis fork and has no entry.
//
// m_isthmusTime carries a compatibility sentinel: unset (UINT64_MAX) means
// "Isthmus is the zero-start baseline", the only shape existing chains have
// (they configure jovian_time / karst_time at most) — resolveOpFork then
// resolves every timestamp below jovian_time to OpFork::Isthmus. An explicitly
// set isthmus_time activates the full Bedrock..Karst ladder for from-genesis
// replay, whose fallback below the earliest scheduled fork is OpFork::Bedrock.
struct OpForkSchedule
{
    uint64_t m_regolithTime = std::numeric_limits<uint64_t>::max();
    uint64_t m_canyonTime = std::numeric_limits<uint64_t>::max();
    uint64_t m_deltaTime = std::numeric_limits<uint64_t>::max();
    uint64_t m_ecotoneTime = std::numeric_limits<uint64_t>::max();
    uint64_t m_fjordTime = std::numeric_limits<uint64_t>::max();
    uint64_t m_graniteTime = std::numeric_limits<uint64_t>::max();
    uint64_t m_holoceneTime = std::numeric_limits<uint64_t>::max();
    uint64_t m_isthmusTime = std::numeric_limits<uint64_t>::max();
    uint64_t m_jovianTime = std::numeric_limits<uint64_t>::max();
    uint64_t m_karstTime = std::numeric_limits<uint64_t>::max();
};

/// Resolves which OP fork a block runs under from the chain's genesis fork schedule
/// ([op_fork_timestamps] in config.genesis) and that block's timestamp IN SECONDS.
/// This is op-node's own keying: rollup.json carries jovian_time / karst_time and
/// `IsJovian(ts)` is `Time != nil && ts >= *Time` (op-node/rollup/types.go), with
/// UINT64_MAX standing in for op-node's nil.
///
/// Latest fork first: Karst when `timestampSec >= m_karstTime`, else Jovian when
/// `>= m_jovianTime`, then Isthmus .. Regolith down the ladder, skipping every entry
/// left at UINT64_MAX ("not scheduled", op-node's nil) — an unscheduled intermediate
/// rung is IMPLIED by a later scheduled fork, never terminal.
///
/// Baseline compatibility: when m_isthmusTime is NOT set (the only shape existing
/// chains have), Isthmus is the zero-start baseline — every timestamp below
/// jovian_time resolves to OpFork::Isthmus and the pre-Isthmus rungs are never
/// consulted, bit-identical to the two-key schedule. When m_isthmusTime IS set the
/// full ladder is live and timestamps before the earliest scheduled fork fall back
/// to OpFork::Bedrock (Bedrock is the genesis fork and has no schedule entry).
///
/// The schedule's non-decreasing order is validated once, at config load
/// (NodeConfig::loadOpForkTimestamps); this function does not re-check it.
///
/// WHICH block's timestamp is the caller's decision and differs per rule — op-geth keys the
/// Holocene extraData decode and the Jovian DA-footprint branch on the PARENT header's time
/// (consensus/misc/eip1559/eip1559.go CalcBaseFee), while the L1-attributes calldata layout
/// and the Jovian payload attributes key on the CHILD's (op-node derive/l1_block_info.go,
/// derive/attributes.go). Callers whose internal timestamps are milliseconds convert through
/// opstack-executor/OpCommon.h's forkTimestampSec.
///
/// This is THE fork-activation parser: every component (bcos-evm configAt, the devp2p
/// OpHeaderValidator, the engine service) must resolve forks through it — a second
/// per-field interpretation (UINT64_MAX = "inactive" instead of "implied") rejects
/// headers the executor accepts, which is exactly the jovian-only-schedule boot-but-
/// never-syncs failure this function exists to prevent. The EIP-2124 fork-id ladder
/// (devp2p eth/OpForkId.h) is deliberately NOT built on this function: fork-id mirrors
/// op-geth's reflection over explicitly-SET ChainConfig fields, so the isthmus-baseline
/// fallback does not apply there.
inline OpFork resolveOpFork(const OpForkSchedule& schedule, uint64_t timestampSec) noexcept
{
    if (timestampSec >= schedule.m_karstTime)
    {
        return OpFork::Karst;
    }
    if (timestampSec >= schedule.m_jovianTime)
    {
        return OpFork::Jovian;
    }
    if (schedule.m_isthmusTime == std::numeric_limits<uint64_t>::max())
    {
        return OpFork::Isthmus;
    }
    if (timestampSec >= schedule.m_isthmusTime)
    {
        return OpFork::Isthmus;
    }
    if (timestampSec >= schedule.m_holoceneTime)
    {
        return OpFork::Holocene;
    }
    if (timestampSec >= schedule.m_graniteTime)
    {
        return OpFork::Granite;
    }
    if (timestampSec >= schedule.m_fjordTime)
    {
        return OpFork::Fjord;
    }
    if (timestampSec >= schedule.m_ecotoneTime)
    {
        return OpFork::Ecotone;
    }
    if (timestampSec >= schedule.m_deltaTime)
    {
        return OpFork::Delta;
    }
    if (timestampSec >= schedule.m_canyonTime)
    {
        return OpFork::Canyon;
    }
    if (timestampSec >= schedule.m_regolithTime)
    {
        return OpFork::Regolith;
    }
    return OpFork::Bedrock;
}
}  // namespace bcos::ledger
