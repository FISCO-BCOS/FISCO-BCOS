// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2022 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <evmc/evmc.hpp>
#include <evmc/hex.hpp>
#include <evmone_precompiles/keccak.hpp>
#include <bit>

namespace evmone
{
using evmc::address;
using evmc::bytes;
using evmc::bytes32;
using evmc::bytes_view;
using namespace evmc::literals;

/// Default type for 256-bit hash.
///
/// Better than ethash::hash256 because has some additional handy constructors.
using hash256 = bytes32;

/// The hash of the empty RLP list, i.e. keccak256({0xc0}).
static constexpr auto EmptyListHash =
    0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347_bytes32;

/// Computes Keccak hash out of input bytes (wrapper of ethash::keccak256).
inline hash256 keccak256(bytes_view data) noexcept
{
    return std::bit_cast<hash256>(ethash::keccak256(data.data(), data.size()));
}
}  // namespace evmone
