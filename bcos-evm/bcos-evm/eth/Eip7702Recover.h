#pragma once

// Shared EIP-7702 authorization signature recovery, used by BOTH the opstack transition
// (OpTransition.cpp) and the eth-path process_authorization_list (eth/state/state.cpp) so the
// two execution paths verify authorizations identically — real ecrecover, not the upstream
// signer-shortcut stub.

#include <bcos-evm/eth/state/hash_utils.hpp>
#include <bcos-evm/eth/state/transaction.hpp>
#include <cstdint>
#include <evmc/evmc.hpp>
#include <evmone_precompiles/secp256k1.hpp>
#include <optional>
#include <span>
#include <test/utils/rlp.hpp>

namespace bcos::evm::eth
{
/// Secp256k1's N/2 is the upper bound of an EIP-2 canonical signature's s value.
inline constexpr auto SECP256K1N_OVER_2 = evmmax::secp256k1::Curve::ORDER / 2;

/// EIP-7702 authorization magic byte (prefix of the signing hash).
inline constexpr uint8_t kSetCodeMagic = 0x05;

/// Recover the authority address from an EIP-7702 authorization via ecrecover.
/// signing hash = keccak256(0x05 || rlp([chain_id, address, nonce]))
inline std::optional<evmc::address> recoverAuthority(const evmone::state::Authorization& auth)
{
    auto msg = evmone::bytes{kSetCodeMagic} +
               evmone::rlp::encode_tuple(auth.chain_id, auth.addr, auth.nonce);
    const auto h = evmone::keccak256(msg);
    const auto r = intx::be::store<evmc::bytes32>(auth.r);
    const auto s = intx::be::store<evmc::bytes32>(auth.s);
    return evmmax::secp256k1::ecrecover(std::span<const uint8_t, 32>{h.bytes, 32},
        std::span<const uint8_t, 32>{r.bytes, 32}, std::span<const uint8_t, 32>{s.bytes, 32},
        auth.v != 0);
}
}  // namespace bcos::evm::eth
