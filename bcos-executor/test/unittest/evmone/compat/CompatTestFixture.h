/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief Shared fixtures for evmone Phase 2 forward-compatibility (Compat) tests.
 *  @file CompatTestFixture.h
 */
#pragma once

#include "bcos-framework/ledger/Features.h"
#include "bcos-utilities/Common.h"
#include <string_view>

namespace bcos::test::compat
{

/// Governance profiles for T32 forward-compatibility matrix (see
/// docs/superpowers/plans/2026-05-19-evmone-phase2-ut-compat-plan.md).
struct CompatFeatureProfile
{
    static ledger::Features legacyLondon()
    {
        ledger::Features f;
        return f;
    }

    static ledger::Features cancunOnly()
    {
        ledger::Features f;
        f.set(ledger::Features::Flag::feature_evm_cancun);
        return f;
    }
};

namespace compat_addr
{
inline constexpr std::string_view MODEXP = "0000000000000000000000000000000000000005";
inline constexpr std::string_view SYSCONFIG = "0000000000000000000000000000000000001000";
}  // namespace compat_addr

/// EIP-198 modexp input (shared by FC-M / FC-P; unity build merges compat/*.cpp).
inline bytes compatMakeModexpInput(bytes base, bytes exp, bytes mod)
{
    bytes input;
    input.resize(input.size() + 32, 0);
    input.back() = static_cast<uint8_t>(base.size());
    input.resize(input.size() + 32, 0);
    input.back() = static_cast<uint8_t>(exp.size());
    input.resize(input.size() + 32, 0);
    input.back() = static_cast<uint8_t>(mod.size());
    input.insert(input.end(), base.begin(), base.end());
    input.insert(input.end(), exp.begin(), exp.end());
    input.insert(input.end(), mod.begin(), mod.end());
    return input;
}

}  // namespace bcos::test::compat
