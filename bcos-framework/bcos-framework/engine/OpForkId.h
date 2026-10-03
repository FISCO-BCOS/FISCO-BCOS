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

#include <cstdint>
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

// The base-fee clock reads this table instead of the fork order (see OpBaseFeeClock):
// "layout != Empty" means Holocene or later, "layout == Jovian17" means Jovian or
// later. These pin each boundary so a layout change cannot silently re-price blocks.
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
