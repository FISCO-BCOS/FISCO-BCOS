#pragma once

#include <cstdint>
#include <evmc/evmc.hpp>
#include <intx/intx.hpp>

namespace evmone::state
{
class StateView;
}

namespace bcos::evm::opstack
{
/// Read from the L1Block storage slots after this block's L1 attributes deposit
/// has executed (consensus-critical).
/// Layout mirrors op-geth L1Block.sol (slot 3/8 are packed, non-standard ABI). Slot numbers
/// cross-checked against op-geth core/types/rollup_cost.go (L1BaseFeeSlot=1,
/// L1FeeScalarsSlot=3, OverheadSlot=5, ScalarSlot=6, L1BlobBaseFeeSlot=7,
/// OperatorFeeParamsSlot=8): number/timestamp pack into slot 0, so basefee is slot 1 — NOT
/// slot 2 — in both the Bedrock and the Ecotone+ layouts.
struct OpFeeParams
{
    intx::uint256 l1_base_fee;             // slot 1 (whole slot)
    uint32_t base_fee_scalar;              // slot 3 bytes[16,20)
    uint32_t blob_base_fee_scalar;         // slot 3 bytes[20,24)
    intx::uint256 blob_base_fee;           // slot 7 (whole slot)
    uint32_t operator_fee_scalar;          // slot 8 bytes[20,24)
    uint64_t operator_fee_constant;        // slot 8 bytes[24,32)
    uint16_t da_footprint_gas_scalar = 0;  // slot 8 bytes[18,20)
    // Bedrock–Delta legacy L1-fee inputs (has_legacy_l1_formula), both whole slots; the scalar's
    // precision is 1e6 (op-geth l1CostHelper divides by oneMillion). Ecotone+ keeps stale
    // Bedrock-era values in these two slots (L1Block.sol @custom:legacy fields) — only the legacy
    // formula may read them.
    intx::uint256 l1_fee_overhead = 0;     // slot 5 (whole slot)
    intx::uint256 l1_fee_scalar = 0;       // slot 6 (whole slot)
};

/// op-geth rollup_cost.go NewL1CostFunc "firstEcotoneBlock" edge case: Ecotone is active (per
/// the fork schedule) but the L1Block predeploy's Ecotone parameters have not been written yet —
/// the activation block's L1 attributes deposit is still Bedrock-formatted, so slots 3/7 read
/// zero. Detected exactly as op-geth does: L1BlobBaseFeeSlot == 0 AND the two 32-bit scalars in
/// L1FeeScalarsSlot (bytes [16:24)) are both zero. When true, the Bedrock legacy formula applies
/// (checked before the Fjord branch — "the first block of Fjord and Ecotone could be the same
/// block"). Never true on a settled chain: a written L1 attributes set carries non-zero scalars.
[[nodiscard]] inline bool ecotoneParamsUnset(const OpFeeParams& p) noexcept
{
    return p.blob_base_fee == 0 && p.base_fee_scalar == 0 && p.blob_base_fee_scalar == 0;
}

/// Unpack from the six storage slots (Isthmus callers may ignore da_footprint_gas_scalar;
/// pre-Ecotone callers ignore the slot 3/7/8 fields).
OpFeeParams unpackOpFeeParams(const evmc::bytes32& slot1, const evmc::bytes32& slot3,
    const evmc::bytes32& slot5, const evmc::bytes32& slot6, const evmc::bytes32& slot7,
    const evmc::bytes32& slot8) noexcept;

/// Read slots 1/3/5/6/7/8 from OP_L1_BLOCK and unpack (a missing slot is treated as a zero word).
OpFeeParams loadOpFeeParams(const evmone::state::StateView& view) noexcept;
}  // namespace bcos::evm::opstack
