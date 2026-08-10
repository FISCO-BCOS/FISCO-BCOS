// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2022 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0
// Portions from evmone test/state/precompiles_internal.hpp: Copyright 2024 The
// evmone Authors.

/// @file EVMPrecompiles.h
/// @brief Ported evmone test/state precompiles (precompiles.hpp +
///        precompiles_internal.hpp), renamed into the
///        `bcos::executor_v1::eth::evm` namespace so the ethereum-executor no
///        longer depends on the bcos-evm library (and never ODR-collides with
///        it in a shared binary).
///
/// The implementation (EVMPrecompiles.cpp) is a verbatim port of the upstream
/// evmone dispatch over the evmone_precompiles primitives.

#pragma once

#include <evmc/evmc.hpp>
#include <cstddef>
#include <cstdint>

namespace bcos::executor_v1::eth::evm
{

/// The precompile identifiers (ported evmone PrecompileId).
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

struct ExecutionResult
{
    evmc_status_code status_code;
    size_t output_size;
};

struct PrecompileAnalysis
{
    int64_t gas_cost;
    size_t max_output_size;
};

PrecompileAnalysis ecrecover_analyze(evmc::bytes_view input, evmc_revision rev) noexcept;
PrecompileAnalysis sha256_analyze(evmc::bytes_view input, evmc_revision rev) noexcept;
PrecompileAnalysis ripemd160_analyze(evmc::bytes_view input, evmc_revision rev) noexcept;
PrecompileAnalysis identity_analyze(evmc::bytes_view input, evmc_revision rev) noexcept;
PrecompileAnalysis expmod_analyze(evmc::bytes_view input, evmc_revision rev) noexcept;
PrecompileAnalysis ecadd_analyze(evmc::bytes_view input, evmc_revision rev) noexcept;
PrecompileAnalysis ecmul_analyze(evmc::bytes_view input, evmc_revision rev) noexcept;
PrecompileAnalysis ecpairing_analyze(evmc::bytes_view input, evmc_revision rev) noexcept;
PrecompileAnalysis blake2bf_analyze(evmc::bytes_view input, evmc_revision rev) noexcept;
PrecompileAnalysis point_evaluation_analyze(evmc::bytes_view input, evmc_revision rev) noexcept;
PrecompileAnalysis bls12_g1add_analyze(evmc::bytes_view input, evmc_revision rev) noexcept;
PrecompileAnalysis bls12_g1msm_analyze(evmc::bytes_view input, evmc_revision rev) noexcept;
PrecompileAnalysis bls12_g2add_analyze(evmc::bytes_view input, evmc_revision rev) noexcept;
PrecompileAnalysis bls12_g2msm_analyze(evmc::bytes_view input, evmc_revision rev) noexcept;
PrecompileAnalysis bls12_pairing_check_analyze(evmc::bytes_view input, evmc_revision rev) noexcept;
PrecompileAnalysis bls12_map_fp_to_g1_analyze(evmc::bytes_view input, evmc_revision rev) noexcept;
PrecompileAnalysis bls12_map_fp2_to_g2_analyze(evmc::bytes_view input, evmc_revision rev) noexcept;
PrecompileAnalysis p256verify_analyze(evmc::bytes_view input, evmc_revision rev) noexcept;

ExecutionResult ecrecover_execute(
    const uint8_t* input, size_t input_size, uint8_t* output, size_t output_size) noexcept;
ExecutionResult sha256_execute(
    const uint8_t* input, size_t input_size, uint8_t* output, size_t output_size) noexcept;
ExecutionResult ripemd160_execute(
    const uint8_t* input, size_t input_size, uint8_t* output, size_t output_size) noexcept;
ExecutionResult identity_execute(
    const uint8_t* input, size_t input_size, uint8_t* output, size_t output_size) noexcept;
ExecutionResult expmod_execute(
    const uint8_t* input, size_t input_size, uint8_t* output, size_t output_size) noexcept;
ExecutionResult ecadd_execute(
    const uint8_t* input, size_t input_size, uint8_t* output, size_t output_size) noexcept;
ExecutionResult ecmul_execute(
    const uint8_t* input, size_t input_size, uint8_t* output, size_t output_size) noexcept;
ExecutionResult blake2bf_execute(
    const uint8_t* input, size_t input_size, uint8_t* output, size_t output_size) noexcept;
ExecutionResult ecpairing_execute(
    const uint8_t* input, size_t input_size, uint8_t* output, size_t output_size) noexcept;
ExecutionResult point_evaluation_execute(
    const uint8_t* input, size_t input_size, uint8_t* output, size_t output_size) noexcept;
ExecutionResult bls12_g1add_execute(
    const uint8_t* input, size_t input_size, uint8_t* output, size_t output_size) noexcept;
ExecutionResult bls12_g1msm_execute(
    const uint8_t* input, size_t input_size, uint8_t* output, size_t output_size) noexcept;
ExecutionResult bls12_g2add_execute(
    const uint8_t* input, size_t input_size, uint8_t* output, size_t output_size) noexcept;
ExecutionResult bls12_g2msm_execute(
    const uint8_t* input, size_t input_size, uint8_t* output, size_t output_size) noexcept;
ExecutionResult bls12_pairing_check_execute(
    const uint8_t* input, size_t input_size, uint8_t* output, size_t output_size) noexcept;
ExecutionResult bls12_map_fp_to_g1_execute(
    const uint8_t* input, size_t input_size, uint8_t* output, size_t output_size) noexcept;
ExecutionResult bls12_map_fp2_to_g2_execute(
    const uint8_t* input, size_t input_size, uint8_t* output, size_t output_size) noexcept;
ExecutionResult p256verify_execute(
    const uint8_t* input, size_t input_size, uint8_t* output, size_t output_size) noexcept;

/// Checks if the address @p addr is considered a precompiled contract in the
/// revision @p rev (ported evmone is_precompile).
bool is_precompile(evmc_revision rev, const evmc::address& addr) noexcept;

/// Executes the message to a precompiled contract (msg.code_address must be a
/// precompile; ported evmone call_precompile).
evmc::Result call_precompile(evmc_revision rev, const evmc_message& msg) noexcept;

}  // namespace bcos::executor_v1::eth::evm
