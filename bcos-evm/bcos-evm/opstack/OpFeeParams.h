#pragma once

#include <bcos-evm/opstack/OpForkSchedule.h>
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
/// Layout mirrors op-geth L1Block.sol (slot 3/8 are packed, non-standard ABI).
struct OpFeeParams
{
    intx::uint256 l1_base_fee;             // slot 1 (whole slot)
    intx::uint256 overhead;                // slot 5 (Bedrock)
    intx::uint256 bedrock_scalar;          // slot 6 (Bedrock)
    uint32_t base_fee_scalar;              // slot 3 bytes[16,20)
    uint32_t blob_base_fee_scalar;         // slot 3 bytes[20,24)
    intx::uint256 blob_base_fee;           // slot 7 (whole slot)
    uint32_t operator_fee_scalar;          // slot 8 bytes[20,24)
    uint64_t operator_fee_constant;        // slot 8 bytes[24,32)
    uint16_t da_footprint_gas_scalar = 0;  // slot 8 bytes[18,20)
};

/// True when the Ecotone-formula input slots are live (op-geth switches formulas on the
/// same probe): a non-zero slot3 scalar segment or a non-zero slot7 blob base fee.
/// When false on an Ecotone-timestamped block, the Pre-Ecotone (Bedrock) formula on
/// slots 1/5/6 still governs (specs.optimism.io/protocol/ecotone/l1-attributes.html:
/// the activation block keeps setL1BlockValues; steady state arrives with the next block).
[[nodiscard]] inline bool ecotoneL1SlotsLive(const OpFeeParams& p) noexcept
{
    return p.base_fee_scalar != 0 || p.blob_base_fee_scalar != 0 || p.blob_base_fee != 0;
}

/// Which L1 formula family a block actually runs: Bedrock on the Bedrock model, and ALSO
/// on the Ecotone activation block itself — that block still executes the legacy
/// setL1BlockValues, so the Ecotone formula's input slots are zero and the zero-probe
/// falls back to the Pre-Ecotone rule (specs.optimism.io/protocol/ecotone/
/// l1-attributes.html: steady state arrives with the next block). Single home for the
/// selection: computeL1Cost (the fee) and deriveOpReceiptMeta (the receipt snapshot) must
/// both consume this helper — a block priced with one formula while its receipt claims
/// the other is a silent fee/receipt split.
[[nodiscard]] inline bool bedrockFormulaActive(
    const OpForkConfig& cfg, const OpFeeParams& fee) noexcept
{
    return cfg.l1_fee_model == L1FeeModel::Bedrock ||
           (cfg.l1_fee_model == L1FeeModel::Ecotone && !ecotoneL1SlotsLive(fee));
}

/// Unpack from the four storage slots (Isthmus callers may ignore da_footprint_gas_scalar).
OpFeeParams unpackOpFeeParams(const evmc::bytes32& slot1, const evmc::bytes32& slot3,
    const evmc::bytes32& slot7, const evmc::bytes32& slot8) noexcept;

/// Read slots 1/3/7/8 from OP_L1_BLOCK and unpack (a missing slot is treated as a zero word).
OpFeeParams loadOpFeeParams(const evmone::state::StateView& view) noexcept;
}  // namespace bcos::evm::opstack
