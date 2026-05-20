/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @file EvmPrecompiledAddress.h
 *  @brief Ethereum-style precompile address helpers for evmone HostContext.
 */
#pragma once

#include <cstdint>
#include <string_view>

namespace bcos::executor
{
namespace
{
inline bool tryParseHexNibble(char c, uint8_t& out)
{
    if (c >= '0' && c <= '9')
    {
        out = static_cast<uint8_t>(c - '0');
        return true;
    }
    if (c >= 'a' && c <= 'f')
    {
        out = static_cast<uint8_t>(c - 'a' + 10);
        return true;
    }
    if (c >= 'A' && c <= 'F')
    {
        out = static_cast<uint8_t>(c - 'A' + 10);
        return true;
    }
    return false;
}

/// @return 40-nibble hex body without 0x prefix, or empty view if length is invalid.
inline std::string_view normalizeHexAddressBody(std::string_view addr)
{
    if (addr.size() >= 2 && (addr.compare(0, 2, "0x") == 0 || addr.compare(0, 2, "0X") == 0))
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

/// @brief True when @p addr is 0x0b..0x11 (EIP-2537 BLS12-381 precompiles).
/// @details Expects 20-byte address as 40 hex nibbles (optional 0x prefix). Leading 19 bytes
///          must be zero; the last byte must be in [0x0b, 0x11]. Parsed without std::stoul.
inline bool isBLSPrecompileAddress(std::string_view addr)
{
    const auto body = normalizeHexAddressBody(addr);
    if (body.empty())
    {
        return false;
    }
    for (size_t i = 0; i < 38; ++i)
    {
        if (body[i] != '0')
        {
            return false;
        }
    }
    uint8_t hi = 0;
    uint8_t lo = 0;
    if (!tryParseHexNibble(body[38], hi) || !tryParseHexNibble(body[39], lo))
    {
        return false;
    }
    const uint8_t lastByte = static_cast<uint8_t>((hi << 4) | lo);
    return lastByte >= 0x0b && lastByte <= 0x11;
}

inline constexpr std::string_view P256VERIFY_PRECOMPILE_ADDRESS =
    "0000000000000000000000000000000000000100";

inline bool isP256verifyPrecompileAddress(std::string_view addr)
{
    const auto body = normalizeHexAddressBody(addr);
    return !body.empty() && body == P256VERIFY_PRECOMPILE_ADDRESS;
}
}  // namespace bcos::executor
