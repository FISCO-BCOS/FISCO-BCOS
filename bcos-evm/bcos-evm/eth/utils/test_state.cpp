// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2024 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0
#include "test_state.hpp"
#include <bcos-evm/eth/state/state.hpp>
#include <bcos-evm/eth/state/system_contracts.hpp>

namespace evmone::test
{
std::optional<state::StateView::Account> TestState::get_account(const address& addr) const noexcept
{
    const auto it = find(addr);
    if (it == end())
        return std::nullopt;

    const auto& acc = it->second;
    // TODO: Cache code hash for MTP root hash calculation?
    return Account{acc.nonce, acc.balance, keccak256(acc.code), !acc.storage.empty()};
}

bytes TestState::get_account_code(const address& addr) const noexcept
{
    const auto it = find(addr);
    if (it == end())
        return {};

    return it->second.code;
}

void TestState::apply(const state::StateDiff& diff)
{
    for (const auto& m : diff.modified_accounts)
    {
        auto& a = (*this)[m.addr];
        a.nonce = m.nonce;
        a.balance = m.balance;
        if (m.code.has_value())
            a.code = *m.code;  // TODO: Consider taking rvalue ref to avoid code copy.
        for (const auto& [k, v] : m.modified_storage)
        {
            if (v)
                a.storage.insert_or_assign(k, v);
            else
                a.storage.erase(k);
        }
    }

    for (const auto& addr : diff.deleted_accounts)
        erase(addr);
}

bytes32 TestState::get_storage(const address& addr, const bytes32& key) const noexcept
{
    const auto ait = find(addr);
    if (ait == end())  // TODO: When?
        return bytes32{};
    const auto& storage = ait->second.storage;
    const auto it = storage.find(key);
    return (it != storage.end()) ? it->second : bytes32{};
}

bytes32 TestBlockHashes::get_block_hash(int64_t block_number) const noexcept
{
    if (const auto& it = find(block_number); it != end())
        return it->second;

    // Convention for testing: if the block hash is unknown return the predefined "fake" value.
    // https://github.com/ethereum/go-ethereum/blob/v1.12.2/tests/state_test_util.go#L432
    const auto s = std::to_string(block_number);
    return keccak256({reinterpret_cast<const uint8_t*>(s.data()), s.size()});
}

[[nodiscard]] std::variant<state::TransactionReceipt, std::error_code> transition(TestState& state,
    const state::BlockInfo& block, const state::BlockHashes& block_hashes,
    const state::Transaction& tx, evmc_revision rev, evmc::VM& vm, int64_t block_gas_left,
    int64_t blob_gas_left)
{
    const auto tx_props_or_error =
        state::validate_transaction(state, block, tx, rev, block_gas_left, blob_gas_left);
    if (const auto err = get_if<std::error_code>(&tx_props_or_error))
        return *err;

    auto receipt = state::transition(state, block, block_hashes, tx, rev, vm,
        get<state::TransactionProperties>(tx_props_or_error));
    state.apply(receipt.state_diff);
    return receipt;
}

void finalize(TestState& state, evmc_revision rev, const address& coinbase,
    std::optional<uint64_t> block_reward, std::span<const state::Ommer> ommers,
    std::span<const state::Withdrawal> withdrawals)
{
    const auto diff = state::finalize(state, rev, coinbase, block_reward, ommers, withdrawals);
    state.apply(diff);
}

void system_call_block_start(TestState& state, const state::BlockInfo& block,
    const state::BlockHashes& block_hashes, evmc_revision rev, evmc::VM& vm)
{
    const auto diff = state::system_call_block_start(state, block, block_hashes, rev, vm);
    state.apply(diff);
}

std::optional<std::vector<state::Requests>> system_call_block_end(TestState& state,
    const state::BlockInfo& block, const state::BlockHashes& block_hashes, evmc_revision rev,
    evmc::VM& vm)
{
    auto result = state::system_call_block_end(state, block, block_hashes, rev, vm);
    if (!result.has_value())
        return std::nullopt;
    state.apply(result->state_diff);
    return std::move(result->requests);
}
}  // namespace evmone::test
