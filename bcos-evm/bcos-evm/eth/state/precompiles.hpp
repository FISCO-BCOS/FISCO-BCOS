// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2022 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <evmc/evmc.hpp>

namespace evmone::state
{
/// The precompile identifiers.
enum class PrecompileId : uint8_t
{
    ecrecover,
    sha256,
    ripemd160,
    identity,
    expmod,
    ecadd,
    ecmul,
    ecpairing,
    blake2bf,
    point_evaluation,
    bls12_g1add,
    bls12_g1msm,
    bls12_g2add,
    bls12_g2msm,
    bls12_pairing_check,
    bls12_map_fp_to_g1,
    bls12_map_fp2_to_g2,
    p256verify,
};

/// Checks if the address @p addr is considered a precompiled contract in the revision @p rev.
bool is_precompile(evmc_revision rev, const evmc::address& addr) noexcept;

/// Executes the message to a precompiled contract (msg.code_address must be a precompile).
evmc::Result call_precompile(evmc_revision rev, const evmc_message& msg) noexcept;
}  // namespace evmone::state
