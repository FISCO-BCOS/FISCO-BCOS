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
 * @brief Lightweight OP fork identity and Engine API profile types.
 */
#pragma once

#include <bcos-framework/engine/Types.h>
#include <bcos-framework/ledger/OpForkSchedule.h>
#include <magic_enum/magic_enum.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>

namespace bcos::engine
{
/// Lightweight OP fork identity for Engine API profile selection (no EVMC types).
/// Integer values are process-local: nothing persists this enum (no ledger key,
/// RLP or canonical key), so inserting historical forks is safe.
enum class OpForkId : uint8_t
{
    Regolith = 0,
    Canyon,
    Ecotone,
    Fjord,
    Granite,
    Holocene,
    Isthmus,
    Jovian,
    Karst,
};

/// The ONE OP fork ladder, as data: one row per OpFork rung, in declaration order,
/// carrying the rung's op-node name and its Engine-API fork id (nullopt for rungs
/// without an Engine API surface: Bedrock predates it, Delta has no EL effect).
/// Every consumer that would otherwise re-list the rungs — the canonical-text name
/// table, the fork->OpForkId mapping — derives from these rows.
struct OpForkLadderRow
{
    bcos::ledger::OpFork fork;
    std::string_view name;
    std::optional<OpForkId> engineForkId;
};

inline constexpr std::array<OpForkLadderRow, 11> c_opForkLadder{{
    {bcos::ledger::OpFork::Bedrock, "bedrock", std::nullopt},
    {bcos::ledger::OpFork::Regolith, "regolith", OpForkId::Regolith},
    {bcos::ledger::OpFork::Canyon, "canyon", OpForkId::Canyon},
    {bcos::ledger::OpFork::Delta, "delta", std::nullopt},
    {bcos::ledger::OpFork::Ecotone, "ecotone", OpForkId::Ecotone},
    {bcos::ledger::OpFork::Fjord, "fjord", OpForkId::Fjord},
    {bcos::ledger::OpFork::Granite, "granite", OpForkId::Granite},
    {bcos::ledger::OpFork::Holocene, "holocene", OpForkId::Holocene},
    {bcos::ledger::OpFork::Isthmus, "isthmus", OpForkId::Isthmus},
    {bcos::ledger::OpFork::Jovian, "jovian", OpForkId::Jovian},
    {bcos::ledger::OpFork::Karst, "karst", OpForkId::Karst},
}};

// Total over the ladder enum, in declaration order, with an Engine-API id for
// exactly the OpForkId rungs.
static_assert(
    [] {
        auto const rungs = magic_enum::enum_values<bcos::ledger::OpFork>();
        if (rungs.size() != c_opForkLadder.size())
        {
            return false;
        }
        for (std::size_t i = 0; i < rungs.size(); ++i)
        {
            if (rungs[i] != c_opForkLadder[i].fork)
            {
                return false;
            }
        }
        return true;
    }(),
    "c_opForkLadder must list every OpFork rung in declaration order");
static_assert(
    [] {
        // A BIJECTION, not a count: every OpForkId value must be carried by exactly one row.
        // The count form let a duplicated id plus an id no row carried through (both keep the
        // number of id-carrying rows equal), and opForkIdFor then answered the duplicated
        // row's id — the wrong Engine-API profile — for the rung whose id was dropped.
        std::array<bool, magic_enum::enum_count<OpForkId>()> seen{};
        for (auto const& row : c_opForkLadder)
        {
            if (!row.engineForkId.has_value())
            {
                continue;
            }
            auto const index = static_cast<std::size_t>(*row.engineForkId);
            if (index >= seen.size() || seen[index])
            {
                return false;
            }
            seen[index] = true;
        }
        for (bool const mapped : seen)
        {
            if (!mapped)
            {
                return false;
            }
        }
        return true;
    }(),
    "c_opForkLadder must map every OpForkId rung exactly once (bijection, not a count)");

[[nodiscard]] inline std::optional<OpForkId> opForkIdFor(bcos::ledger::OpFork fork)
{
    for (auto const& row : c_opForkLadder)
    {
        if (row.fork == fork)
        {
            return row.engineForkId;
        }
    }
    return std::nullopt;
}

/// Shape of a block's extraData for an OP fork: empty before Holocene, the
/// 9-byte Holocene 1559 params, or the 17-byte Jovian form with minBaseFee.
/// Selected by the block's own timestamp, unlike the baseFee clock (see
/// OpBaseFee.h), which reads the parent's extraData.
enum class OpExtraDataLayout : uint8_t
{
    Empty,
    Holocene9,
    Jovian17,
};

/// Engine API method versions permitted for a fork at a given timestamp.
struct EngineApiProfile
{
    ApiVersion forkchoiceUpdated{};
    ApiVersion getPayload{};
    ApiVersion newPayload{};
};

enum class OpForkResolutionError : uint8_t
{
    UnsupportedTimestamp,
    InconsistentExecutionConfig,
};

struct EngineForkContext
{
    OpForkId forkId{};
    EngineApiProfile api{};
    bool hasDaFootprint = false;
    OpExtraDataLayout extraDataLayout = OpExtraDataLayout::Empty;
};

/// op-node Config.NewPayloadVersion / GetPayloadVersion / ForkchoiceUpdatedVersion
/// (rollup/types.go), plus this repo's Karst getPayload V5. Fjord and Granite add no
/// Engine API surface, so they carry Ecotone's methods; they stay distinct ids for
/// extraData and baseFee.
[[nodiscard]] inline constexpr EngineApiProfile engineApiProfileFor(OpForkId forkId)
{
    switch (forkId)
    {
    case OpForkId::Regolith:
        return {.forkchoiceUpdated = ApiVersion::V1,
            .getPayload = ApiVersion::V2,
            .newPayload = ApiVersion::V2};
    case OpForkId::Canyon:
        return {.forkchoiceUpdated = ApiVersion::V2,
            .getPayload = ApiVersion::V2,
            .newPayload = ApiVersion::V2};
    case OpForkId::Ecotone:
    case OpForkId::Fjord:
    case OpForkId::Granite:
    case OpForkId::Holocene:
        return {.forkchoiceUpdated = ApiVersion::V3,
            .getPayload = ApiVersion::V3,
            .newPayload = ApiVersion::V3};
    case OpForkId::Isthmus:
    case OpForkId::Jovian:
        return {.forkchoiceUpdated = ApiVersion::V3,
            .getPayload = ApiVersion::V4,
            .newPayload = ApiVersion::V4};
    case OpForkId::Karst:
        return {.forkchoiceUpdated = ApiVersion::V3,
            .getPayload = ApiVersion::V5,
            .newPayload = ApiVersion::V4};
    }
    return {};
}

[[nodiscard]] inline constexpr OpExtraDataLayout extraDataLayoutFor(OpForkId forkId)
{
    switch (forkId)
    {
    case OpForkId::Regolith:
    case OpForkId::Canyon:
    case OpForkId::Ecotone:
    case OpForkId::Fjord:
    case OpForkId::Granite:
        return OpExtraDataLayout::Empty;
    case OpForkId::Holocene:
    case OpForkId::Isthmus:
        return OpExtraDataLayout::Holocene9;
    case OpForkId::Jovian:
    case OpForkId::Karst:
        return OpExtraDataLayout::Jovian17;
    }
    return OpExtraDataLayout::Empty;
}

// Fork -> extraData layout, the layout-side reference for the base-fee clock's boundary
// flags: "layout != Empty" means Holocene or later, "layout == Jovian17" means Jovian or
// later. The base-fee helpers (OpBaseFee.h) still derive those flags from the fork schedule —
// wiring that seam to this table is a follow-up, so no production reader exists yet. These
// pin each boundary so a layout change cannot silently re-price blocks.
static_assert(extraDataLayoutFor(OpForkId::Regolith) == OpExtraDataLayout::Empty &&
              extraDataLayoutFor(OpForkId::Canyon) == OpExtraDataLayout::Empty &&
              extraDataLayoutFor(OpForkId::Ecotone) == OpExtraDataLayout::Empty &&
              extraDataLayoutFor(OpForkId::Fjord) == OpExtraDataLayout::Empty &&
              extraDataLayoutFor(OpForkId::Granite) == OpExtraDataLayout::Empty &&
              extraDataLayoutFor(OpForkId::Holocene) == OpExtraDataLayout::Holocene9 &&
              extraDataLayoutFor(OpForkId::Isthmus) == OpExtraDataLayout::Holocene9 &&
              extraDataLayoutFor(OpForkId::Jovian) == OpExtraDataLayout::Jovian17 &&
              extraDataLayoutFor(OpForkId::Karst) == OpExtraDataLayout::Jovian17);

using EngineForkResolution = std::variant<EngineForkContext, OpForkResolutionError>;
}  // namespace bcos::engine
