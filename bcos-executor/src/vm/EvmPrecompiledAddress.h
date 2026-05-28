/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief EVM precompile address helpers (BLS / p256verify gating).
 *  @file EvmPrecompiledAddress.h
 */
#pragma once

#include <cstring>
#include <string_view>

namespace bcos::executor
{
namespace
{
/// @return 40-nibble lowercase hex body without 0x prefix, or empty view if length is invalid.
/// @details Incoming contract addresses are normalized to lowercase before this helper runs.
inline std::string_view normalizeHexAddressBody(std::string_view addr)
{
    if (addr.size() >= 2 && (addr.starts_with("0x") || addr.starts_with("0X")))
    {
        addr.remove_prefix(2);
    }
    if (addr.size() != 40)
    {
        return {};
    }
    return addr;
}
}  // namespace

inline constexpr std::string_view P256VERIFY_PRECOMPILED_ADDRESS =
    "0000000000000000000000000000000000000100";

inline constexpr std::string_view BLS_ADDRESS_ZERO_PREFIX =
    "00000000000000000000000000000000000000";

/// @brief True when @p addr is 0x0b..0x11 (EIP-2537 BLS12-381 precompiles).
/// @details Expects canonical lowercase 40-nibble hex (optional 0x prefix). Leading 19 bytes
///          must be zero; the last byte must be in [0x0b, 0x11].
inline bool isBLSPrecompileAddress(std::string_view addr)
{
    const auto body = normalizeHexAddressBody(addr);
    if (body.empty())
    {
        return false;
    }
    if (std::memcmp(body.data(), BLS_ADDRESS_ZERO_PREFIX.data(), BLS_ADDRESS_ZERO_PREFIX.size()) !=
        0)
    {
        return false;
    }
    const char hi = body[38];
    const char lo = body[39];
    if (hi == '0' && lo >= 'b' && lo <= 'f')
    {
        return true;
    }
    if (hi == '1' && lo >= '0' && lo <= '1')
    {
        return true;
    }
    return false;
}

inline bool isP256verifyPrecompileAddress(std::string_view addr)
{
    const auto body = normalizeHexAddressBody(addr);
    return !body.empty() && body == P256VERIFY_PRECOMPILED_ADDRESS;
}
}  // namespace bcos::executor
