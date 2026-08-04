#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpReceiptMeta.h>
#include <bcos-evm/opstack/RollupCost.h>

namespace bcos::evm::opstack
{
OpReceiptMeta deriveOpReceiptMeta(const OpForkConfig& cfg, const OpFeeParams& fee, uint32_t flzLen,
    intx::uint256 l1_cost, intx::uint256 operator_fee_at_used, bool fill_operator_scalars) noexcept
{
    OpReceiptMeta m;
    m.l1_gas_price = fee.l1_base_fee;
    m.l1_blob_base_fee = fee.blob_base_fee;
    m.l1_base_fee_scalar = fee.base_fee_scalar;
    m.l1_blob_base_fee_scalar = fee.blob_base_fee_scalar;
    m.l1_fee = l1_cost;
    if (cfg.has_operator_fee)
    {
        m.operator_fee = operator_fee_at_used;
        if (fill_operator_scalars &&
            (fee.operator_fee_scalar != 0 || fee.operator_fee_constant != 0))
        {
            m.operator_fee_scalar = fee.operator_fee_scalar;
            m.operator_fee_constant = fee.operator_fee_constant;
        }
    }
    if (cfg.has_da_footprint)
    {
        const auto scalar = static_cast<uint64_t>(fee.da_footprint_gas_scalar);
        m.da_footprint_gas_scalar = scalar;
        m.da_footprint = estimatedDaSizeFromFlz(flzLen) * scalar;
        m.l1_gas_used = estimatedL1GasUsedFromFlz(flzLen);
    }
    return m;
}
}  // namespace bcos::evm::opstack
