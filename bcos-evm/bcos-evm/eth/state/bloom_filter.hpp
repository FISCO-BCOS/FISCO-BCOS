// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2023 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "hash_utils.hpp"
#include <span>

namespace evmone::state
{
struct Log;
struct TransactionReceipt;

/// The 2048-bit hash suitable for keeping an Ethereum bloom filter of transactions logs.
struct BloomFilter
{
    //// The 256 bytes of the bloom filter value.
    uint8_t bytes[256] = {};

    /// Implicit operator converting to bytes_view.
    constexpr operator bytes_view() const noexcept { return {bytes, sizeof(bytes)}; }
};

/// Computes combined bloom fitter for set of logs.
/// It's used to compute bloom filter for single transaction.
[[nodiscard]] BloomFilter compute_bloom_filter(std::span<const Log> logs) noexcept;

/// Computes combined bloom fitter for set of TransactionReceipts
/// It's used to compute bloom filter for a block.
[[nodiscard]] BloomFilter compute_bloom_filter(
    std::span<const TransactionReceipt> receipts) noexcept;

/// Loads BloomFilter from bytes_view
BloomFilter bloom_filter_from_bytes(const bytes_view& data) noexcept;

}  // namespace evmone::state
