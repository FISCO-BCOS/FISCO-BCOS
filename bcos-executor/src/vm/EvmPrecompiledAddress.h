/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @file EvmPrecompiledAddress.h
 *  @brief Ethereum-style precompile address constants and helpers for evmone HostContext.
 */
#pragma once

#include <evmc/evmc.h>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace bcos::executor
{

/// 40-nibble hex body prefix shared by all Ethereum precompiles (see also
/// precompiled::EVM_PRECOMPILED_PREFIX in PrecompiledTypeDef.h).
inline constexpr std::string_view EVM_PRECOMPILED_ADDRESS_PREFIX =
    "000000000000000000000000000000000000000";

// Classic precompiles (0x01–0x09)
inline constexpr std::string_view ECRECOVER_PRECOMPILE_ADDRESS =
    "0000000000000000000000000000000000000001";
inline constexpr std::string_view SHA256_PRECOMPILE_ADDRESS =
    "0000000000000000000000000000000000000002";
inline constexpr std::string_view RIPEMD160_PRECOMPILE_ADDRESS =
    "0000000000000000000000000000000000000003";
inline constexpr std::string_view IDENTITY_PRECOMPILE_ADDRESS =
    "0000000000000000000000000000000000000004";
inline constexpr std::string_view MODEXP_PRECOMPILE_ADDRESS =
    "0000000000000000000000000000000000000005";
inline constexpr std::string_view ALT_BN128_G1_ADD_PRECOMPILE_ADDRESS =
    "0000000000000000000000000000000000000006";
inline constexpr std::string_view ALT_BN128_G1_MUL_PRECOMPILE_ADDRESS =
    "0000000000000000000000000000000000000007";
inline constexpr std::string_view ALT_BN128_PAIRING_PRECOMPILE_ADDRESS =
    "0000000000000000000000000000000000000008";
inline constexpr std::string_view BLAKE2F_PRECOMPILE_ADDRESS =
    "0000000000000000000000000000000000000009";

// EIP-4844 point evaluation (Cancun)
inline constexpr std::string_view POINT_EVALUATION_PRECOMPILE_ADDRESS =
    "000000000000000000000000000000000000000a";
inline constexpr uint8_t POINT_EVALUATION_PRECOMPILE_INDEX = 0x0a;

// EIP-2537 BLS12-381 (Prague)
inline constexpr std::string_view BLS_G1ADD_PRECOMPILE_ADDRESS =
    "000000000000000000000000000000000000000b";
inline constexpr std::string_view BLS_G1MSM_PRECOMPILE_ADDRESS =
    "000000000000000000000000000000000000000c";
inline constexpr std::string_view BLS_G2ADD_PRECOMPILE_ADDRESS =
    "000000000000000000000000000000000000000d";
inline constexpr std::string_view BLS_G2MSM_PRECOMPILE_ADDRESS =
    "000000000000000000000000000000000000000e";
inline constexpr std::string_view BLS_PAIRING_CHECK_PRECOMPILE_ADDRESS =
    "000000000000000000000000000000000000000f";
inline constexpr std::string_view BLS_MAP_FP_TO_G1_PRECOMPILE_ADDRESS =
    "0000000000000000000000000000000000000010";
inline constexpr std::string_view BLS_MAP_FP2_TO_G2_PRECOMPILE_ADDRESS =
    "0000000000000000000000000000000000000011";
inline constexpr uint8_t BLS_PRECOMPILE_INDEX_FIRST = 0x0b;
inline constexpr uint8_t BLS_PRECOMPILE_INDEX_LAST = 0x11;

// EIP-7951 p256verify (Osaka)
inline constexpr std::string_view P256VERIFY_PRECOMPILE_ADDRESS =
    "0000000000000000000000000000000000000100";
inline constexpr uint32_t P256VERIFY_PRECOMPILE_INDEX = 0x100;

// Backward-compat alias for callers still using the old spelling.
inline constexpr std::string_view P256VERIFY_PRECOMPILED_ADDRESS = P256VERIFY_PRECOMPILE_ADDRESS;

inline constexpr std::string_view BLS_ADDRESS_ZERO_PREFIX =
    "00000000000000000000000000000000000000";

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

/// @brief True when @p addr is 0x0a (EIP-4844 point evaluation precompile, Cancun).
/// @details Expects canonical lowercase 40-nibble hex (optional 0x prefix).
inline bool isPointEvaluationPrecompileAddress(std::string_view addr)
{
    const auto body = normalizeHexAddressBody(addr);
    return !body.empty() && body == POINT_EVALUATION_PRECOMPILE_ADDRESS;
}

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
    return !body.empty() && body == P256VERIFY_PRECOMPILE_ADDRESS;
}

inline bool isModexpPrecompileAddress(std::string_view addr)
{
    const auto body = normalizeHexAddressBody(addr);
    return !body.empty() && body == MODEXP_PRECOMPILE_ADDRESS;
}

inline bool isModexpPrecompileEvmcAddress(evmc_address const& addr) noexcept
{
    for (size_t i = 0; i < 19; ++i)
    {
        if (addr.bytes[i] != 0)
        {
            return false;
        }
    }
    return addr.bytes[19] == 0x05;
}

}  // namespace bcos::executor
