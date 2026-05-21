#pragma once

#include "Eip7702Delegation.h"
#include "Web3Eip7702Fill.h"
#include <bcos-crypto/interfaces/crypto/Hash.h>
#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>
#include <optional>

namespace bcos::executor
{

/// True when @p code is exactly the 23-byte EIP-7702 delegation designator (0xEF0100‖target).
bool isEip7702DelegationIndicator(bcos::bytesConstRef code) noexcept;

/// Recover authority address from an authorization tuple (secp256k1 only; EIP-7702 §3.3).
std::optional<bcos::Address> recoverEip7702Authority(
    crypto::Hash::Ptr const& hashImpl, Eip7702Authorization const& auth);

evmc_address addressToEvmc(bcos::Address const& addr) noexcept;

/// @p tupleChainId == 0 means "any chain"; otherwise must match configured ledger chain id.
bool eip7702ChainIdMatches(
    uint64_t tupleChainId, std::optional<evmc_uint256be> const& ledgerChainId) noexcept;

}  // namespace bcos::executor
