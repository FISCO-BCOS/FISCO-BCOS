#pragma once

#include <cstdint>
#include <evmc/bytes.hpp>
#include <intx/intx.hpp>

namespace bcos::evmref::opstack
{
struct OpFeeParams;
struct OpForkConfig;

/// FastLZ compressed length (Fjord DA regression input). Ported from op-geth
/// FlzCompressLen; byte-for-byte aligned with production.
uint32_t flzCompressLen(evmc::bytes_view data) noexcept;

/// estimatedSize (×1e6) = max(100e6, -42585600 + 836500*fastlzSize).
intx::uint256 estimatedDaSizeScaled(uint32_t fastlzSize) noexcept;

/// estimatedSize = estimatedDaSizeScaled(flz) / 1e6; returns 0 when flzLen==0
/// (reused when flz is already computed, to avoid recompressing).
uint64_t estimatedDaSizeFromFlz(uint32_t flzLen) noexcept;

/// estimatedSize = estimatedDaSizeScaled(flz) / 1e6; returns 0 for an empty envelope.
uint64_t estimatedDaSize(evmc::bytes_view signedTxEnvelope) noexcept;

/// Ecotone L1 calldata gas: zeroes*4 + nonZeroes*16 (no pre-Regolith +68).
uint64_t bedrockCalldataGasUsed(evmc::bytes_view signedTxEnvelope) noexcept;

/// L1 data fee, Fjord+ FastLZ branch: returns 0 when flzLen==0 (reused when flz is already
/// computed, to avoid recompressing).
intx::uint256 computeL1CostFromFlz(
    const OpFeeParams& params, uint32_t flzLen, const OpForkConfig& cfg) noexcept;

/// L1 data fee. Ecotone (has_ecotone_l1_formula) uses the calldataGas formula; Fjord+ uses FastLZ.
/// Returns 0 for an empty envelope; the caller guarantees deposits are always zero.
intx::uint256 computeL1Cost(
    const OpFeeParams& params, evmc::bytes_view signedTxEnvelope, const OpForkConfig& cfg) noexcept;

/// Operator fee: Isthmus gas*scalar/1e6+constant; Jovian gas*scalar*100+constant.
intx::uint256 computeOperatorCost(
    const OpFeeParams& params, uint64_t gas, const OpForkConfig& cfg) noexcept;
}  // namespace bcos::evmref::opstack
