#include <bcos-evm/opstack/RollupCost.h>

#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <cstdint>

namespace bcos::evm::opstack
{
namespace
{
constexpr int64_t kFjordDivisor = 1000000000000;
constexpr int64_t kNonzeroByteCost = 16;
constexpr int64_t kZeroByteCost = 4;
// Bedrock–Delta legacy L1 fee scalar precision (op-geth l1CostHelper's oneMillion). Same value
// as the operator scalar divisor but a different quantity — do not merge them.
constexpr int64_t kLegacyFeeScalarDivisor = 1000000;
}  // namespace

uint64_t estimatedDaSize(evmc::bytes_view signedTxEnvelope) noexcept
{
    if (signedTxEnvelope.empty())
        return 0;
    return estimatedDaSizeFromFlz(flzCompressLen(signedTxEnvelope));
}

uint64_t bedrockCalldataGasUsed(evmc::bytes_view env) noexcept
{
    uint64_t zeroes = 0;
    uint64_t nonZeroes = 0;
    for (const auto b : env)
        (b == 0 ? zeroes : nonZeroes)++;
    return zeroes * static_cast<uint64_t>(kZeroByteCost) +
           nonZeroes * static_cast<uint64_t>(kNonzeroByteCost);
}

uint64_t legacyTxDataGas(evmc::bytes_view env, bool regolithActive) noexcept
{
    // op-geth newL1CostFuncBedrockHelper: pre-Regolith the calldata count carries a one-time
    // +68 phantom non-zero bytes ((ones + 68) * 16); Regolith drops it (rollup_cost.go).
    return bedrockCalldataGasUsed(env) +
           (regolithActive ? 0 : 68 * static_cast<uint64_t>(kNonzeroByteCost));
}

LegacyL1Cost computeLegacyL1Cost(
    const OpFeeParams& params, evmc::bytes_view signedTxEnvelope, bool regolithActive) noexcept
{
    const auto gasUsed = intx::uint512{legacyTxDataGas(signedTxEnvelope, regolithActive)} +
                         intx::uint512{params.l1_fee_overhead};
    const auto truncatedGasUsed = static_cast<uint64_t>(gasUsed);  // op-geth big.Int.Uint64()

    // op-geth l1CostHelper: fee = gasUsed * l1BaseFee * scalar / 1e6 in big.Int (no wrap).
    // A true fee < 2^256 implies the full product < 2^256 * 1e6 << 2^512, so a 512-bit
    // multiplication overflow implies fee >= 2^256: saturate (same rule as the Ecotone/Fjord
    // paths — the opValidate balance cap rejects a fee this large either way).
    constexpr auto kMax512 = ~intx::uint512{0};
    const auto baseFee = intx::uint512{params.l1_base_fee};
    const auto scalar = intx::uint512{params.l1_fee_scalar};
    if ((baseFee != 0 && gasUsed > kMax512 / baseFee) ||
        (scalar != 0 && gasUsed * baseFee > kMax512 / scalar))
        return {~intx::uint256{0}, truncatedGasUsed};
    const auto fee = gasUsed * baseFee * scalar / intx::uint512{kLegacyFeeScalarDivisor};
    if (fee > intx::uint512{~intx::uint256{0}})
        return {~intx::uint256{0}, truncatedGasUsed};
    return {static_cast<intx::uint256>(fee), truncatedGasUsed};
}

intx::uint256 computeL1CostFromFlz(
    const OpFeeParams& params, uint32_t flzLen, const OpForkConfig& cfg) noexcept
{
    (void)cfg;
    if (flzLen == 0)
        return intx::uint256{0};
    // 512-bit like the opValidate balance cap: the two whole-slot fee reads let these products
    // cross 2^256, where op-geth's big.Int evaluation does not wrap. Saturate on return — a fee
    // >= 2^256 exceeds any representable balance, so the cap rejects it either way.
    const auto calldataPerByte = intx::umul(params.l1_base_fee,
        intx::uint256{params.base_fee_scalar} * intx::uint256{kNonzeroByteCost});
    const auto blobPerByte =
        intx::umul(params.blob_base_fee, intx::uint256{params.blob_base_fee_scalar});
    // op-geth Fjord+:
    // estimatedDaSizeScaled(flz)*(l1BaseFee*16*baseScalar+blobBaseFee*blobScalar)/1e12
    const auto scaled = estimatedDaSizeScaled(flzLen);
    const auto fee =
        (calldataPerByte + blobPerByte) * intx::uint512{scaled} / intx::uint512{kFjordDivisor};
    if (fee > intx::uint512{~intx::uint256{0}})
        return ~intx::uint256{0};
    return static_cast<intx::uint256>(fee);
}

intx::uint256 computeL1Cost(
    const OpFeeParams& params, evmc::bytes_view signedTxEnvelope, const OpForkConfig& cfg) noexcept
{
    if (signedTxEnvelope.empty())
        return intx::uint256{0};

    const bool bedrock = bedrockFormulaActive(cfg, params);
    if (bedrock)
    {
        // op-geth newL1CostFuncBedrockHelper / l1CostHelper (exec-engine Pre-Ecotone):
        //   (rollupDataGas + overhead) * l1BaseFee * scalar / 1e6, evaluated in that order.
        // Delegated to the legacy formula with regolithActive=true — Bedrock-tier blocks are
        // past Regolith, so the pre-Regolith +68 phantom-byte correction stays off, and the
        // saturating multiply/divide order matches l1CostHelper. One implementation, not a
        // fork-twin that silently dropped the +68 switch.
        return computeLegacyL1Cost(params, signedTxEnvelope, /*regolithActive=*/true).fee;
    }

    if (cfg.l1_fee_model == L1FeeModel::Ecotone)
    {
        // op-geth newL1CostFuncEcotone:
        //   calldataGas*(l1BaseFee*16*baseScalar + blobBaseFee*blobScalar)/16e6
        const auto calldataPerByte = intx::umul(params.l1_base_fee,
            intx::uint256{params.base_fee_scalar} * intx::uint256{kNonzeroByteCost});
        const auto blobPerByte =
            intx::umul(params.blob_base_fee, intx::uint256{params.blob_base_fee_scalar});
        const auto calldataGas = intx::uint256{bedrockCalldataGasUsed(signedTxEnvelope)};
        const auto fee = (calldataPerByte + blobPerByte) * intx::uint512{calldataGas} /
                         intx::uint512{16'000'000};
        if (fee > intx::uint512{~intx::uint256{0}})
            return ~intx::uint256{0};
        return static_cast<intx::uint256>(fee);
    }

    // Fjord+ (current implementation):
    //   estimatedDaSizeScaled(flz)*(l1BaseFee*16*baseScalar + blobBaseFee*blobScalar)/1e12
    return computeL1CostFromFlz(params, flzCompressLen(signedTxEnvelope), cfg);
}

intx::uint256 computeOperatorCost(
    const OpFeeParams& params, uint64_t gas, bool jovianFormula) noexcept
{
    if (jovianFormula)
    {
        return intx::uint256{gas} * intx::uint256{params.operator_fee_scalar} *
                   intx::uint256{detail::c_jovianOperatorFeeMultiplier} +
               intx::uint256{params.operator_fee_constant};
    }
    return intx::uint256{gas} * intx::uint256{params.operator_fee_scalar} /
               intx::uint256{detail::c_operatorFeeScalarDivisor} +
           intx::uint256{params.operator_fee_constant};
}

intx::uint256 computeOperatorCost(
    const OpFeeParams& params, uint64_t gas, const OpForkConfig& cfg) noexcept
{
    return computeOperatorCost(params, gas, cfg.has_jovian_operator_formula);
}
}  // namespace bcos::evm::opstack
