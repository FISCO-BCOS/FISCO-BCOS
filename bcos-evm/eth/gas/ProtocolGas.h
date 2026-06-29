/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Canonical Ethereum protocol gas constants (geth params.go aligned).
 * @file ProtocolGas.h
 */
#pragma once

#include <cstdint>

namespace bcos::evm::gas
{

inline constexpr int64_t TX_BASE_GAS = 21'000;
inline constexpr int64_t CREATE_BASE_GAS = 32'000;
inline constexpr int64_t INITCODE_WORD_GAS = 2;
inline constexpr int64_t ACCESS_LIST_ADDRESS_COST = 2'400;
inline constexpr int64_t ACCESS_LIST_STORAGE_KEY_COST = 1'900;
inline constexpr int64_t ZERO_BYTE_INTRINSIC_COST = 4;
inline constexpr int64_t NONZERO_BYTE_INTRINSIC_COST = 16;

}  // namespace bcos::evm::gas
