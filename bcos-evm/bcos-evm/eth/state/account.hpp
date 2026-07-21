// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2021 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <evmc/evmc.hpp>
#include <intx/intx.hpp>
#include <unordered_map>

namespace evmone::state
{
using evmc::address;
using evmc::bytes;
using evmc::bytes32;
using namespace evmc::literals;

/// The representation of the account storage value.
struct StorageValue
{
    /// The current value.
    bytes32 current;

    /// The original value.
    bytes32 original;

    evmc_access_status access_status = EVMC_ACCESS_COLD;
};

/// The state account.
struct Account
{
    /// The maximum allowed nonce value.
    static constexpr auto NonceMax = std::numeric_limits<uint64_t>::max();

    /// The keccak256 hash of the empty input. Used to identify empty account's code.
    static constexpr auto EMPTY_CODE_HASH =
        0xc5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470_bytes32;

    /// The account nonce.
    uint64_t nonce = 0;

    /// The account balance.
    intx::uint256 balance;

    bytes32 code_hash = EMPTY_CODE_HASH;

    /// If the account has non-empty initial storage (when accessing the cold account).
    bool has_initial_storage = false;

    /// The cached and modified account storage entries.
    std::unordered_map<bytes32, StorageValue> storage;

    /// The EIP-1153 transient (transaction-level lifetime) storage.
    std::unordered_map<bytes32, bytes32> transient_storage;

    /// The cache of the account code.
    ///
    /// Check code_hash to know if an account code is empty.
    /// Empty here only means it has not been loaded from the initial storage.
    bytes code;

    /// The account has been destructed and should be erased at the end of a transaction.
    bool destructed = false;

    /// The account should be erased if it is empty at the end of a transaction.
    /// This flag means the account has been "touched" as defined in EIP-161,
    /// or it is a newly created temporary account.
    ///
    /// Yellow Paper uses term "delete" but it is a keyword in C++ while
    /// the term "erase" is used for deleting objects from C++ collections.
    bool erase_if_empty = false;

    /// The account has been created in the current transaction.
    bool just_created = false;

    // This account's code has been modified.
    bool code_changed = false;

    evmc_access_status access_status = EVMC_ACCESS_COLD;

    [[nodiscard]] bool is_empty() const noexcept
    {
        return nonce == 0 && balance == 0 && code_hash == EMPTY_CODE_HASH;
    }
};
}  // namespace evmone::state
