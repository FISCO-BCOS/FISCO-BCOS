// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2024 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <evmc/evmc.hpp>
#include <intx/intx.hpp>
#include <vector>

namespace evmone::state
{
using evmc::address;
using evmc::bytes;
using evmc::bytes32;
using intx::uint256;

/// Collection of changes to the State
struct StateDiff
{
    struct Entry
    {
        /// Address of the modified account.
        address addr;

        /// New nonce value.
        /// TODO: Currently it is not guaranteed the value is different from the initial one.
        uint64_t nonce;

        /// New balance value.
        /// TODO: Currently it is not guaranteed the value is different from the initial one.
        uint256 balance;

        /// New or modified account code. If bytes are empty, it means the code has been cleared.
        std::optional<bytes> code;

        /// The list of the account's storage modifications: key => new value.
        /// The value 0 means the storage entry is deleted.
        std::vector<std::pair<bytes32, bytes32>> modified_storage;
    };

    /// List of modified or created accounts.
    std::vector<Entry> modified_accounts;

    /// List of deleted accounts.
    ///
    /// This list doesn't have common addresses with modified_accounts.
    /// Note that from the Cancun revision (because of the modification to the SELFDESTRUCT)
    /// accounts cannot be deleted and this list is always empty.
    std::vector<address> deleted_accounts;
};
}  // namespace evmone::state
