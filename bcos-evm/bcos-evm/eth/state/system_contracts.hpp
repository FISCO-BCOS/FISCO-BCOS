// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2023 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "requests.hpp"
#include <evmc/evmc.hpp>

namespace evmone::state
{
using namespace evmc::literals;

/// The address of the sender of the system calls (EIP-4788).
constexpr auto SYSTEM_ADDRESS = 0xfffffffffffffffffffffffffffffffffffffffe_address;

/// The address of the system contract storing the root hashes of beacon chain blocks (EIP-4788).
constexpr auto BEACON_ROOTS_ADDRESS = 0x000F3df6D732807Ef1319fB7B8bB8522d0Beac02_address;

/// The address of the system contract storing historical block hashes (EIP-2935).
constexpr auto HISTORY_STORAGE_ADDRESS = 0x0000F90827F1C53A10CB7A02335B175320002935_address;

/// The address of the system contract processing EL-triggerable withdrawals (EIP-7002).
constexpr auto WITHDRAWAL_REQUEST_ADDRESS = 0x00000961EF480EB55E80D19AD83579A64C007002_address;

/// The address of the system contract processing consolidations (EIP-7251).
constexpr auto CONSOLIDATION_REQUEST_ADDRESS = 0x0000BBDDC7CE488642FB579F8B00F3A590007251_address;

struct BlockInfo;
struct StateDiff;
class BlockHashes;
class StateView;

/// Performs the system call: invokes system contracts that have to be executed
/// at the start of the block.
///
/// Executes code of pre-defined accounts via pseudo-transaction from the system sender (0xff...fe).
/// The sender's nonce is not increased.
[[nodiscard]] StateDiff system_call_block_start(const StateView& state_view, const BlockInfo& block,
    const BlockHashes& block_hashes, evmc_revision rev, evmc::VM& vm);

struct RequestsResult
{
    StateDiff state_diff;            ///< State diff of the system contracts execution.
    std::vector<Requests> requests;  ///< Collected requests.
};

/// Performs the system call: invokes system contracts that have to be executed
/// at the end of the block.
///
/// Executes code of pre-defined accounts via pseudo-transaction from the system sender (0xff...fe).
/// The sender's nonce is not increased.
/// @return The collected requests and state diff or std::nullopt if the execution has failed.
[[nodiscard]] std::optional<RequestsResult> system_call_block_end(const StateView& state_view,
    const BlockInfo& block, const BlockHashes& block_hashes, evmc_revision rev, evmc::VM& vm);
}  // namespace evmone::state
