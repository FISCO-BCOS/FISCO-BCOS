#pragma once

#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/multiprecision/cpp_int.hpp>
#include <cstdint>
#include <vector>

namespace bcos::protocol
{

/// EIP-7702 authorization list entry.
/// Represents a single delegation authorization: the signer authorizes
/// its code to be set to the delegation designator (0xef0100 || address).
struct Authorization
{
    uint64_t chainId = 0;          // EIP-155 chain ID (0 = any)
    bcos::Address address;         // 20-byte delegation target address
    uint64_t nonce = 0;            // signer's nonce at authorization time
    bcos::Address signer;          // 20-byte recovered signer address
    bcos::u256 r{};                // ECDSA signature r
    bcos::u256 s{};                // ECDSA signature s
    uint8_t v = 0;                 // y_parity (0 or 1 for EIP-7702)
};

using AuthorizationList = std::vector<Authorization>;

/// EIP-4844 blob versioned hash (32-byte commitment hash).
using VersionedHash = bcos::h256;
using VersionedHashes = std::vector<VersionedHash>;

}  // namespace bcos::protocol
