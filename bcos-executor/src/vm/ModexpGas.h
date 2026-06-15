/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief Modexp (0x05) precompile gas and EIP-7823 validation.
 *  @file ModexpGas.h
 */
#pragma once

#include "bcos-framework/ledger/Features.h"
#include "bcos-utilities/Common.h"
#include <evmc/evmc.h>
#include <cstddef>
#include <string_view>

namespace bcos::executor
{

struct ModexpLengths
{
    bool overflow = false;
    size_t baseLen = 0;
    size_t expLen = 0;
    size_t modLen = 0;
};

constexpr size_t MODEXP_MAX_FIELD_LEN_EIP7823 = 1024;

ModexpLengths parseModexpLengths(bcos::bytesConstRef input);

inline bool modexpEip7823Enabled(ledger::Features const& features, evmc_revision revision) noexcept
{
    return revision >= EVMC_OSAKA && features.get(ledger::Features::Flag::feature_evm_osaka);
}

bool validateModexpEip7823(bcos::bytesConstRef input, evmc_revision revision);

bool shouldRejectModexpEip7823(evmc_address const& addr, bcos::bytesConstRef input,
    ledger::Features const& features, evmc_revision revision) noexcept;

bool shouldRejectModexpEip7823(std::string_view addr, bcos::bytesConstRef input,
    ledger::Features const& features, evmc_revision revision) noexcept;

/// EIP-198 (< Berlin), EIP-2565 (Berlin..Osaka-1), EIP-7883 (Osaka+).
bcos::bigint calcModexpGas(bcos::bytesConstRef input, evmc_revision revision);

/// Legacy EIP-198 pricing for PrecompiledRegistrar::pricer("modexp") and unit tests only.
/// Production uses PrecompiledContract::modexp() -> calcModexpGas(input, revision).
bcos::bigint calcModexpGasEip198Public(bcos::bytesConstRef input);

}  // namespace bcos::executor
