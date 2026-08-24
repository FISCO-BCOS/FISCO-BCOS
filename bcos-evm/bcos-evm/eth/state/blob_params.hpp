// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2025 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <evmc/evmc.hpp>

namespace evmone::state
{
/// The cost of a single blob in gas units (EIP-4844).
constexpr auto GAS_PER_BLOB = 0x20000;  // 2**17

/// The maximum number of blobs that can be included in a transaction (EIP-7594).
constexpr auto MAX_TX_BLOB_COUNT = 6;

/// The blob schedule entry for an EVM revision (EIP-7840).
struct BlobParams
{
    uint16_t target = 0;
    uint16_t max = 0;
    uint32_t base_fee_update_fraction = 0;
};
}  // namespace evmone::state
