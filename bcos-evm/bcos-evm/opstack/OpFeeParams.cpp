#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/eth/state/state_view.hpp>
#include <cassert>

namespace bcos::evm::opstack
{
namespace
{
// Read [byteOff, byteOff+len) of a big-endian word as an unsigned integer (len <= 8).
uint64_t readBE(const evmc::bytes32& w, size_t byteOff, size_t len) noexcept
{
    assert(len <= 8 && byteOff + len <= sizeof(w.bytes));
    uint64_t v = 0;
    for (size_t i = 0; i < len; ++i)
    {
        v = (v << 8) | w.bytes[byteOff + i];
    }
    return v;
}
}  // namespace

OpFeeParams unpackOpFeeParams(const evmc::bytes32& slot1, const evmc::bytes32& slot3,
    const evmc::bytes32& slot7, const evmc::bytes32& slot8) noexcept
{
    return OpFeeParams{
        .l1_base_fee = intx::be::load<intx::uint256>(slot1),
        .base_fee_scalar = static_cast<uint32_t>(readBE(slot3, 16, 4)),
        .blob_base_fee_scalar = static_cast<uint32_t>(readBE(slot3, 20, 4)),
        .blob_base_fee = intx::be::load<intx::uint256>(slot7),
        .operator_fee_scalar = static_cast<uint32_t>(readBE(slot8, 20, 4)),
        .operator_fee_constant = readBE(slot8, 24, 8),
        .da_footprint_gas_scalar = static_cast<uint16_t>(readBE(slot8, 18, 2)),
    };
}
OpFeeParams loadOpFeeParams(const evmone::state::StateView& view) noexcept
{
    auto slot = [](uint8_t s) {
        evmc::bytes32 k{};
        k.bytes[31] = s;
        return k;
    };
    return unpackOpFeeParams(view.get_storage(OP_L1_BLOCK, slot(1)),
        view.get_storage(OP_L1_BLOCK, slot(3)), view.get_storage(OP_L1_BLOCK, slot(7)),
        view.get_storage(OP_L1_BLOCK, slot(8)));
}
}  // namespace bcos::evm::opstack
