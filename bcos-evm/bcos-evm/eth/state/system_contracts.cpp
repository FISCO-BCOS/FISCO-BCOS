// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2023 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0

#include "system_contracts.hpp"
#include "host.hpp"
#include "state_view.hpp"

#include <stdexcept>

namespace evmone::state
{
namespace
{
/// Information about a registered "storage" system contract. They are executed at the block start
/// to store additional information in the State.
struct StorageSystemContract
{
    using GetInputFn = bytes32(const BlockInfo&, const BlockHashes&) noexcept;

    evmc_revision since = EVMC_MAX_REVISION;  ///< EVM revision in which added.
    address addr;                             ///< Address of the system contract.
    GetInputFn* get_input = nullptr;          ///< How to get the input for the system call.

    /// Whether a failed call to this contract fails the whole block. Mirrors op-geth
    /// core/state_processor.go: ProcessBeaconBlockRoot (EIP-4788) *ignores* the call error
    /// (`_, _, _ = evm.Call(...)`), while ProcessParentBlockHash (EIP-2935) *panics* on error
    /// (`if err != nil { panic(err) }`). Writes are rolled back on failure either way (see
    /// §6.4 D-2).
    bool fatal_on_failure = true;
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
        },
        /*fatal_on_failure=*/false},
    StorageSystemContract{EVMC_PRAGUE, HISTORY_STORAGE_ADDRESS,
        [](const BlockInfo& block, const BlockHashes& block_hashes) noexcept {
            return block_hashes.get_block_hash(block.number - 1);
        },
        /*fatal_on_failure=*/true},
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
    for (const auto& [since, addr, get_input, fatal_on_failure] : STORAGE_SYSTEM_CONTRACTS)
    {
        if (rev < since)
            break;  // Because entries are ordered, there are no other contracts for this revision.

        // Skip the call if the target account doesn't exist. This is by EIP-4788 spec.
        // > if no code exists at [address], the call must fail silently.
        const auto code = state_view.get_account_code(addr);
        if (code.empty())
            continue;

        const auto input32 = get_input(block, block_hashes);
        // Checkpoint before the system call so we can roll back dirty state on failure.
        // Unlike op-geth's evm.Call (which Snapshots internally), execute_system_call
        // wraps vm.execute directly with no journal boundary — REVERT/OOG leaves SSTORE
        // side effects in State, and the previous assert() was compiled away in Release
        // (RelWithDebInfo → -DNDEBUG). See §6.4 D-2.
        const auto cp = state.checkpoint();
        const auto res =
            execute_system_call(state, block, block_hashes, rev, vm, addr, code, input32);
        if (res.status_code != EVMC_SUCCESS)
        {
            state.rollback(cp);
            if (fatal_on_failure)
            {
                // Fatal contract (EIP-2935 history-storage): op-geth ProcessParentBlockHash
                // panics on error, i.e. a local/internal fault → the scheduler classifies it as
                // OpStorageError (-32603), NOT a block-level rejection. Thrown as std::logic_error
                // (NOT runtime_error): this build's RTTI quirk (libevmone -fno-rtti non-unique
                // std::exception typeinfo) makes runtime_error escape the scheduler's typed catch
                // into catch(...) → OpConsensusError → INVALID. logic_error binds the typed catch,
                // whose local-fault branch produces OpStorageError (§6.4 D-2 / D-4).
                throw std::logic_error(
                    "system_call_block_start: system contract call failed with status " +
                    std::to_string(res.status_code));
            }
            // Non-fatal contract (EIP-4788 beacon-roots): op-geth ProcessBeaconBlockRoot does
            // `_, _, _ = evm.Call(...)` — the error is ignored, writes already rolled back above,
            // the block proceeds exactly as if the call had succeeded.
        }
    }
    // TODO: Should we return empty diff if no system contracts?
    return state.build_diff(rev);
}

std::optional<RequestsResult> system_call_block_end(const StateView& state_view,
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
            return std::nullopt;

        const auto cp = state.checkpoint();
        const auto res = execute_system_call(state, block, block_hashes, rev, vm, addr, code, {});
        if (res.status_code != EVMC_SUCCESS)
        {
            state.rollback(cp);
            return std::nullopt;
        }
        requests.emplace_back(request_type, bytes_view{res.output_data, res.output_size});
    }
    return RequestsResult{state.build_diff(rev), requests};
}
}  // namespace evmone::state
