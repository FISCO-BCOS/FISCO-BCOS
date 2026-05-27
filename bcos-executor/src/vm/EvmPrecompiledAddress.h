/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief EVM precompile address helpers (BLS / p256verify gating).
 *  @file EvmPrecompiledAddress.h
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace bcos::executor
{

inline constexpr std::string_view P256VERIFY_PRECOMPILED_ADDRESS =
    "0000000000000000000000000000000000000100";

/// Prague-gated BLS12-381 precompiles at 0x0b–0x11 (40-hex address, no 0x prefix).
inline bool isBLSPrecompileAddress(std::string_view addr)
{
    std::string a(addr);
    if (a.size() == 42 && a[0] == '0' && (a[1] == 'x' || a[1] == 'X'))
    {
        a = a.substr(2);
    }
    if (a.size() != 40)
    {
        return false;
    }
    for (size_t i = 0; i < 38; ++i)
    {
        if (a[i] != '0')
        {
            return false;
        }
    }
    const auto last = static_cast<uint8_t>(std::stoul(a.substr(38, 2), nullptr, 16));
    return last >= 0x0b && last <= 0x11;
}

}  // namespace bcos::executor
