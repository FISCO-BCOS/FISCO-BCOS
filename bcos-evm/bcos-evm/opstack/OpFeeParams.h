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
/// Layout mirrors op-geth L1Block.sol (slot 3/8 are packed, non-standard ABI).
struct OpFeeParams
{
    intx::uint256 l1_base_fee;             // slot 1 (whole slot)
    uint32_t base_fee_scalar;              // slot 3 bytes[16,20)
    uint32_t blob_base_fee_scalar;         // slot 3 bytes[20,24)
    intx::uint256 blob_base_fee;           // slot 7 (whole slot)
    uint32_t operator_fee_scalar;          // slot 8 bytes[20,24)
    uint64_t operator_fee_constant;        // slot 8 bytes[24,32)
    uint16_t da_footprint_gas_scalar = 0;  // slot 8 bytes[18,20)
};

/// Unpack from the four storage slots (Isthmus callers may ignore da_footprint_gas_scalar).
OpFeeParams unpackOpFeeParams(const evmc::bytes32& slot1, const evmc::bytes32& slot3,
    const evmc::bytes32& slot7, const evmc::bytes32& slot8) noexcept;

/// Read slots 1/3/7/8 from OP_L1_BLOCK and unpack (a missing slot is treated as a zero word).
OpFeeParams loadOpFeeParams(const evmone::state::StateView& view) noexcept;
}  // namespace bcos::evm::opstack
