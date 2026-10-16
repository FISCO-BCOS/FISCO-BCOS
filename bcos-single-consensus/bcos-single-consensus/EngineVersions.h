/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @file EngineVersions.h
 * @brief Engine API version triple selection for the built-in single-node CL.
 */

#pragma once

#include "bcos-framework/engine/Types.h"
#include <evmc/evmc.h>
#include <cstdint>
#include <optional>

namespace bcos::single_consensus
{
/// Engine API version triple (forkchoiceUpdated / getPayload / newPayload) for a chain EVM
/// revision, mirroring how geth derives the method versions from the fork:
///   Cancun+      -> V3 attributes (parentBeaconBlockRoot) — the Karst dialect's getPayloadV5
///                   / newPayloadV4 response and commit shapes stay valid there;
///   Shanghai     -> V2 (withdrawals, no parentBeaconBlockRoot);
///   pre-Shanghai -> V1 (neither field exists).
/// An unknown revision (executor_version < 2 chains carry no on-chain evmc_revision row)
/// keeps the historical Karst dialect (V3/V5/V4). The produced blocks' execution semantics
/// are still governed by the ledger's executor version, not by these versions.
struct EngineVersions
{
    std::uint32_t forkchoice;
    std::uint32_t getPayload;
    std::uint32_t newPayload;

    bool operator==(EngineVersions const&) const = default;
};

inline EngineVersions engineVersionsFor(std::optional<evmc_revision> revision)
{
    constexpr std::uint32_t v1 = static_cast<std::uint32_t>(bcos::engine::ApiVersion::V1);
    constexpr std::uint32_t v2 = static_cast<std::uint32_t>(bcos::engine::ApiVersion::V2);
    constexpr std::uint32_t v3 = static_cast<std::uint32_t>(bcos::engine::ApiVersion::V3);
    constexpr std::uint32_t v4 = static_cast<std::uint32_t>(bcos::engine::ApiVersion::V4);
    constexpr std::uint32_t v5 = static_cast<std::uint32_t>(bcos::engine::ApiVersion::V5);
    if (!revision.has_value() || *revision >= EVMC_CANCUN)
    {
        return {v3, v5, v4};
    }
    if (*revision >= EVMC_SHANGHAI)
    {
        return {v2, v2, v2};
    }
    return {v1, v1, v1};
}
}  // namespace bcos::single_consensus
