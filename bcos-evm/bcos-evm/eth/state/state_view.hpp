// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2024 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <evmc/evmc.hpp>
#include <intx/intx.hpp>
#include <optional>

namespace evmone::state
{
using evmc::address;
using evmc::bytes;
using evmc::bytes32;
using intx::uint256;

class StateView
{
public:
    struct Account
    {
        uint64_t nonce = 0;
        uint256 balance;
        bytes32 code_hash;
        bool has_storage = false;
    };

    virtual ~StateView() = default;
    virtual std::optional<Account> get_account(const address& addr) const noexcept = 0;
    virtual bytes get_account_code(const address& addr) const noexcept = 0;
    virtual bytes32 get_storage(const address& addr, const bytes32& key) const noexcept = 0;
};


/// Interface to access hashes of known block headers.
class BlockHashes
{
public:
    virtual ~BlockHashes() = default;

    /// Returns the hash of the block header of the given block number.
    virtual bytes32 get_block_hash(int64_t block_number) const noexcept = 0;
};
}  // namespace evmone::state
