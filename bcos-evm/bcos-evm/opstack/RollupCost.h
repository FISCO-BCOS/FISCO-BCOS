#pragma once

#include <cstdint>
#include <evmc/bytes.hpp>
#include <intx/intx.hpp>

namespace bcos::evm::opstack
{
struct OpFeeParams;
struct OpForkConfig;

/// FastLZ-compressed length (the Fjord DA regression input). Port of op-geth FlzCompressLen;
/// byte-for-byte aligned with production.
uint32_t flzCompressLen(evmc::bytes_view data) noexcept;

/// estimatedSize (x1e6) = max(100e6, -42585600 + 836500*fastlzSize).
intx::uint256 estimatedDaSizeScaled(uint32_t fastlzSize) noexcept;

/// estimatedSize = estimatedDaSizeScaled(flz) / 1e6; takes an already-computed flz so the
/// caller need not re-compress.
///
/// DELIBERATE DIVERGENCE FROM op-geth, and the one thing to know before reusing this: flzLen==0
/// returns 0, whereas op-geth has no such branch and its clamp would yield 100
/// (estimatedDASizeScaled floors at MinTransactionSizeScaled = 100e6). flz is 0 only for empty
/// input, which is not a transaction — op-geth never evaluates the formula there, so this is a
/// choice about undefined territory rather than a mismatch on any real input, and charging a
/// 100-byte minimum for no data would be the stranger answer.
///
/// It matters because these two entry points take a raw flz rather than bytes. A block-level DA
/// accumulator that passes a cached or defaulted 0 gets 0 here, silently, instead of the
/// clamped minimum. If that is ever a real caller, validate flz at the call site — do not
/// "fix" this to match the clamp, which would start charging for transactions that carry no
/// data. EstimatedDaSizeDividesScaledBy1e6 pins both halves.
uint64_t estimatedDaSizeFromFlz(uint32_t flzLen) noexcept;

/// estimatedSize = estimatedDaSizeScaled(flz) / 1e6; returns 0 for an empty envelope (same
/// deliberate divergence as estimatedDaSizeFromFlz above).
uint64_t estimatedDaSize(evmc::bytes_view signedTxEnvelope) noexcept;

/// Ecotone L1 calldata gas: zeroes*4 + nonZeroes*16 (no pre-Regolith +68).
uint64_t bedrockCalldataGasUsed(evmc::bytes_view signedTxEnvelope) noexcept;

/// L1 data fee, Fjord+ FastLZ branch; takes an already-computed flz so the caller need not
/// re-compress. flzLen==0 returns 0 — same deliberate divergence from op-geth's clamp as
/// estimatedDaSizeFromFlz above, and the same caveat for callers holding a cached flz.
intx::uint256 computeL1CostFromFlz(
    const OpFeeParams& params, uint32_t flzLen, const OpForkConfig& cfg) noexcept;

/// L1 data fee. Ecotone (has_ecotone_l1_formula) uses the calldataGas formula; Fjord+ uses FastLZ.
/// Returns 0 for an empty envelope; the caller guarantees deposits are always zero.
intx::uint256 computeL1Cost(
    const OpFeeParams& params, evmc::bytes_view signedTxEnvelope, const OpForkConfig& cfg) noexcept;

/// Operator fee: Isthmus gas*scalar/1e6+constant; Jovian gas*scalar*100+constant.
/// The bool overload takes the formula selection directly so a caller can pin it to a snapshot
/// (OpTxProperties) and stay consistent across the validate/transition split; the cfg overload
/// forwards cfg.has_jovian_operator_formula.
intx::uint256 computeOperatorCost(
    const OpFeeParams& params, uint64_t gas, bool jovianFormula) noexcept;
intx::uint256 computeOperatorCost(
    const OpFeeParams& params, uint64_t gas, const OpForkConfig& cfg) noexcept;

/// Charged operator fee, mirroring the block-transition gate (OpTransition.cpp:395):
/// has_operator_fee ? computeOperatorCost(...) : 0. The fork-level 0-gate lives here for
/// callers (runners / tests) that must not re-implement the block-transition decision.
intx::uint256 computeChargedOperatorCost(
    const OpFeeParams& params, uint64_t gas, const OpForkConfig& cfg) noexcept;
}  // namespace bcos::evm::opstack
