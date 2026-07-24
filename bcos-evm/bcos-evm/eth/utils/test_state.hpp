// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2024 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <evmc/evmc.hpp>
#include <intx/intx.hpp>
#include <bcos-evm/eth/state/state_view.hpp>
#include <map>
#include <span>
#include <variant>

namespace evmone
{
namespace state
{
struct BlockInfo;
struct Ommer;
struct Requests;
struct StateDiff;
struct Transaction;
struct TransactionReceipt;
struct Withdrawal;
}  // namespace state

namespace test
{
using evmc::address;
using evmc::bytes;
using evmc::bytes32;
using intx::uint256;

/// Ethereum account representation for tests.
struct TestAccount
{
    uint64_t nonce = 0;
    uint256 balance;
    std::map<bytes32, bytes32> storage;
    bytes code;

    bool operator==(const TestAccount&) const noexcept = default;
};

/// Ethereum State representation for tests.
///
/// This is a simplified variant of state::State:
/// it hides some details related to transaction execution (e.g. original storage values)
/// and is also easier to work with in tests.
class TestState : public state::StateView, public std::map<address, TestAccount>
{
public:
    using map::map;

    std::optional<Account> get_account(const address& addr) const noexcept override;
    bytes get_account_code(const address& addr) const noexcept override;
    bytes32 get_storage(const address& addr, const bytes32& key) const noexcept override;

    /// Apply the state changes.
    void apply(const state::StateDiff& diff);
};

class TestBlockHashes : public state::BlockHashes, public std::unordered_map<int64_t, bytes32>
{
public:
    using std::unordered_map<int64_t, bytes32>::unordered_map;

    bytes32 get_block_hash(int64_t block_number) const noexcept override;
};

/// Wrapping of state::transition() which operates on TestState.
[[nodiscard]] std::variant<state::TransactionReceipt, std::error_code> transition(TestState& state,
    const state::BlockInfo& block, const state::BlockHashes& block_hashes,
    const state::Transaction& tx, evmc_revision rev, evmc::VM& vm, int64_t block_gas_left,
    int64_t blob_gas_left);

/// Wrapping of state::finalize() which operates on TestState.
void finalize(TestState& state, evmc_revision rev, const address& coinbase,
    std::optional<uint64_t> block_reward, std::span<const state::Ommer> ommers,
    std::span<const state::Withdrawal> withdrawals);

/// Wrapping of state::system_call_block_start() which operates on TestState.
void system_call_block_start(TestState& state, const state::BlockInfo& block,
    const state::BlockHashes& block_hashes, evmc_revision rev, evmc::VM& vm);

/// Wrapping of state::system_call_block_end() which operates on TestState.
std::optional<std::vector<state::Requests>> system_call_block_end(TestState& state,
    const state::BlockInfo& block, const state::BlockHashes& block_hashes, evmc_revision rev,
    evmc::VM& vm);
}  // namespace test
}  // namespace evmone
