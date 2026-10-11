// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2023 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0

#include "system_contracts.hpp"
#include "errors.hpp"
#include "host.hpp"
#include "state_view.hpp"
#include <evmone/constants.hpp>

namespace evmone::state
{
namespace
{
/// Convert an address to a 32-byte value, left-padded with zeros.
/// TODO: Deduplicate with to_bytes32 in test/utils/utils.hpp.
bytes32 to_bytes32(const address& addr) noexcept
{
    bytes32 res{};
    std::copy_n(addr.bytes, sizeof(addr), &res.bytes[sizeof(res) - sizeof(addr)]);
    return res;
}

/// Information about a registered "storage" system contract. They are executed at the block start
/// to store additional information in the State.
struct StorageSystemContract
{
    using GetInputFn = bytes32(const BlockInfo&, const BlockHashes&) noexcept;

    evmc_revision since = EVMC_MAX_REVISION;  ///< EVM revision in which added.
    address addr;                             ///< Address of the system contract.
    GetInputFn* get_input = nullptr;          ///< How to get the input for the system call.
};

/// Information about a registered "requests" system contract. They are executed at the block end
/// and produce requests: typed sequence of bytes.
struct RequestsSystemContract
{
    evmc_revision since = EVMC_MAX_REVISION;                ///< EVM revision in which added.
    address addr;                                           ///< Address of the system contract.
    Requests::Type request_type = Requests::Type::deposit;  ///< Type of requests produced.
};

/// Registered "storage" system contracts.
constexpr std::array STORAGE_SYSTEM_CONTRACTS{
    StorageSystemContract{EVMC_CANCUN, BEACON_ROOTS_ADDRESS,
        [](const BlockInfo& block, const BlockHashes&) noexcept {
            return block.parent_beacon_block_root;
        }},
    StorageSystemContract{EVMC_PRAGUE, HISTORY_STORAGE_ADDRESS,
        [](const BlockInfo& block, const BlockHashes& block_hashes) noexcept {
            return block_hashes.get_block_hash(block.number - 1);
        }},
};

/// Registered "requests" system contracts.
constexpr std::array REQUESTS_SYSTEM_CONTRACTS{
    RequestsSystemContract{
        EVMC_PRAGUE,
        WITHDRAWAL_REQUEST_ADDRESS,
        Requests::Type::withdrawal,
    },
    RequestsSystemContract{
        EVMC_PRAGUE,
        CONSOLIDATION_REQUEST_ADDRESS,
        Requests::Type::consolidation,
    },
};

constexpr auto by_rev = [](const auto& a, const auto& b) noexcept { return a.since < b.since; };
static_assert(std::ranges::is_sorted(STORAGE_SYSTEM_CONTRACTS, by_rev),
    "system contract entries must be ordered by revision");
static_assert(std::ranges::is_sorted(REQUESTS_SYSTEM_CONTRACTS, by_rev),
    "system contract entries must be ordered by revision");


evmc::Result execute_system_call(State& state, const BlockInfo& block,
    const BlockHashes& block_hashes, evmc_revision rev, evmc::VM& vm, const address& addr,
    bytes_view code, bytes_view input)
{
    const evmc_message msg{
        .kind = EVMC_CALL,
        .gas = 30'000'000,
        .state_gas = 16 * STORAGE_SET_STATE_GAS,  // Additional state-gas (EIP-8037).
        .recipient = addr,
        .sender = SYSTEM_ADDRESS,
        .input_data = input.data(),
        .input_size = input.size(),
    };

    const Transaction empty_tx{};
    Host host{rev, vm, state, block, block_hashes, empty_tx};
    return vm.execute(host, rev, msg, code.data(), code.size());
}
}  // namespace

StateDiff system_call_block_start(const StateView& state_view, const BlockInfo& block,
    const BlockHashes& block_hashes, evmc_revision rev, evmc::VM& vm)
{
    State state{state_view};
    for (const auto& [since, addr, get_input] : STORAGE_SYSTEM_CONTRACTS)
    {
        if (rev < since)
            break;  // Because entries are ordered, there are no other contracts for this revision.

        // Skip the call if the target account doesn't exist. This is by EIP-4788 spec.
        // > if no code exists at [address], the call must fail silently.
        const auto code = state_view.get_account_code(addr);
        if (code.empty())
            continue;

        const auto input32 = get_input(block, block_hashes);
        const auto res =
            execute_system_call(state, block, block_hashes, rev, vm, addr, code, input32);
        assert(res.status_code == EVMC_SUCCESS);
    }
    // TODO: Should we return empty diff if no system contracts?
    return state.build_diff(rev);
}

std::variant<RequestsResult, std::error_code> system_call_block_end(const StateView& state_view,
    const BlockInfo& block, const BlockHashes& block_hashes, evmc_revision rev, evmc::VM& vm)
{
    State state{state_view};
    std::vector<Requests> requests;
    for (const auto& [since, addr, request_type] : REQUESTS_SYSTEM_CONTRACTS)
    {
        if (rev < since)
            break;  // Because entries are ordered, there are no other contracts for this revision.

        // Fail if the target account doesn't exist. This is by EIP-7002 and EIP-7251 spec.
        const auto code = state_view.get_account_code(addr);
        if (code.empty())
            return make_error_code(SYSTEM_CONTRACT_EMPTY);

        const auto res = execute_system_call(state, block, block_hashes, rev, vm, addr, code, {});
        if (res.status_code != EVMC_SUCCESS)
            return make_error_code(SYSTEM_CONTRACT_CALL_FAILED);
        requests.emplace_back(request_type, bytes_view{res.output_data, res.output_size});
    }
    return RequestsResult{state.build_diff(rev), requests};
}

void emit_transfer_log(
    std::vector<Log>& logs, const address& sender, const address& recipient, const uint256& amount)
{
    /// The ETH transfer log topic (EIP-7708): keccak256("Transfer(address,address,uint256)")
    constexpr auto TRANSFER_EVENT_TOPIC =
        0xddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef_bytes32;

    if (amount == 0)  // No log for 0 value transfers.
        return;

    if (sender == recipient)  // No log for self transfers (balance unchanged).
        return;

    logs.push_back({SYSTEM_ADDRESS, bytes{intx::be::store<uint256be>(amount)},
        {TRANSFER_EVENT_TOPIC, to_bytes32(sender), to_bytes32(recipient)}});
}
}  // namespace evmone::state
