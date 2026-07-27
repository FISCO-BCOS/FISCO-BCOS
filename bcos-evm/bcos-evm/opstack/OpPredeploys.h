#pragma once

#include <evmc/evmc.hpp>

namespace bcos::evm::opstack
{
using evmc::literals::operator""_address;

// OP Stack predeploy / vault addresses (mirrors op-geth; byte-for-byte cross-checked against the
// production OpStackConstants.h).
inline constexpr evmc::address OP_L1_BLOCK = 0x4200000000000000000000000000000000000015_address;
inline constexpr evmc::address OP_GAS_PRICE_ORACLE =
    0x420000000000000000000000000000000000000f_address;
inline constexpr evmc::address OP_SEQUENCER_FEE_VAULT =
    0x4200000000000000000000000000000000000011_address;
inline constexpr evmc::address OP_BASE_FEE_VAULT =
    0x4200000000000000000000000000000000000019_address;
inline constexpr evmc::address OP_L1_FEE_VAULT = 0x420000000000000000000000000000000000001a_address;
inline constexpr evmc::address OP_OPERATOR_FEE_VAULT =
    0x420000000000000000000000000000000000001b_address;
// Synthetic deposit sender (0xdead...0001; not a predeploy, no account pre-seeded).
inline constexpr evmc::address OP_DEPOSITOR = 0xdeaddeaddeaddeaddeaddeaddeaddeaddead0001_address;

/// L2ToL1MessagePasser -- Isthmus withdrawalsRoot = its storage root
/// (op-geth params/protocol_params.go:31; validation side block_validator.go:190-198)
inline constexpr evmc::address OP_L2_TO_L1_MESSAGE_PASSER =
    0x4200000000000000000000000000000000000016_address;
}  // namespace bcos::evm::opstack
