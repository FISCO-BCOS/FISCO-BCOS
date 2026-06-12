/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief Modexp (0x05) precompile gas and EIP-7823 validation.
 *  @file ModexpGas.h
 */
#pragma once

#include "bcos-utilities/Common.h"
#include <evmc/evmc.h>
#include <cstddef>

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
bool validateModexpEip7823(bcos::bytesConstRef input, evmc_revision revision);

/// EIP-198 (< Berlin), EIP-2565 (Berlin..Osaka-1), EIP-7883 (Osaka+).
bcos::bigint calcModexpGas(bcos::bytesConstRef input, evmc_revision revision);

/// Registrar / legacy pricer path (always EIP-198).
bcos::bigint calcModexpGasEip198Public(bcos::bytesConstRef input);

}  // namespace bcos::executor
