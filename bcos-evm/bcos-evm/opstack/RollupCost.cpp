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

/// fee = (value * a * b) / divisor, saturating to uint256 max.
///
/// 512-bit intermediates, with a guard before each multiply. The unguarded form wrapped mod
/// 2^512 and charged a too-small fee: with (overhead, l1BaseFee, scalar) = (0, 2^255, 2^255)
/// and calldataGas 480 the true fee is ~2^498, yet 480 * 2^255 * 2^255 == 120 * 2^512 == 0
/// (mod 2^512), so the Bedrock arm charged zero. A zero factor keeps the mathematical result
/// of zero, matching op-geth's big.Int evaluation rather than saturating.
[[nodiscard]] intx::uint256 saturatingL1Fee(
    intx::uint512 value, intx::uint256 a, intx::uint256 b, intx::uint256 divisor) noexcept
{
    constexpr intx::uint256 c_maxU256 = ~intx::uint256{0};
    const intx::uint512 max512 = ~intx::uint512{0};
    if (a == 0 || b == 0 || value == 0)
    {
        return intx::uint256{0};
    }
    if (intx::uint512{a} > max512 / value)
    {
        return c_maxU256;  // value*a >= 2^512, so the fee is far above uint256 max
    }
    const intx::uint512 first = value * intx::uint512{a};
    if (intx::uint512{b} > max512 / first)
    {
        return c_maxU256;
    }
    const intx::uint512 fee = first * intx::uint512{b} / intx::uint512{divisor};
    if (fee > intx::uint512{c_maxU256})
    {
        return c_maxU256;
    }
    return static_cast<intx::uint256>(fee);
}

intx::uint256 computeL1Cost(
    const OpFeeParams& params, evmc::bytes_view signedTxEnvelope, const OpForkConfig& cfg) noexcept
{
    if (signedTxEnvelope.empty())
        return intx::uint256{0};

    // Ecotone-timestamped blocks keep the Pre-Ecotone formula until the new-formula slots go
    // live: the Ecotone activation block still runs setL1BlockValues (specs.optimism.io/
    // protocol/ecotone/l1-attributes.html), so slot3 scalars and slot7 are still zero and the
    // formula selection falls back on the same zero-probe op-geth uses.
    const bool bedrock = cfg.l1_fee_model == L1FeeModel::Bedrock ||
                         (cfg.l1_fee_model == L1FeeModel::Ecotone && !ecotoneL1SlotsLive(params));
    if (bedrock)
    {
        // op-geth newL1CostFuncBedrockHelper / l1CostHelper (exec-engine Pre-Ecotone):
        //   (rollupDataGas + overhead) * l1BaseFee * scalar / 1e6, evaluated in that order.
        // The sum is widened before the multiply: `overhead` is a whole-slot read, so
        // (calldataGas + overhead) can itself cross 2^256.
        const intx::uint512 gasPlusOverhead =
            intx::uint512{bedrockCalldataGasUsed(signedTxEnvelope)} +
            intx::uint512{params.overhead};
        return saturatingL1Fee(
            gasPlusOverhead, params.l1_base_fee, params.bedrock_scalar, intx::uint256{1'000'000});
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
