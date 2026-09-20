/// @file EthSystemCalls.h
/// @brief Cancun/Prague block-level system calls (EIP-4788 beacon roots and
///        EIP-2935 historical block hashes at block start; EIP-7002/7251
///        execution-layer requests at block end), ported from evmone's
///        eth/state/system_contracts.cpp onto the ethereum-executor's own
///        primitives — EthereumState writes the contracts' storage updates
///        straight into the BCOS storage view via applyToStorage(), so there
///        is no evmone::state::StateView / BlockHashes / StateDiff adapter
///        layer and no bcos-evm dependency.
///
/// Parity notes vs the evmone reference:
///   * same contract tables, same revision gating, same call shape: a raw
///     EVMC_CALL from SYSTEM_ADDRESS with 30M gas, executed with vm.execute()
///     directly (no host.call() — no nonce bump, no value transfer, no
///     recipient touch, no access-list warm-up);
///   * the host runs in system-call mode (tx == nullptr): zero tx origin and
///     zero gas prices, matching upstream's `const Transaction empty_tx{}`.
///     The four system contracts never execute ORIGIN/GASPRICE/CHAINID, so
///     chain id is also 0 here, as upstream's empty tx yields;
///   * EIP-2935's input is the parent block hash, supplied by the caller —
///     upstream computes it as block_hashes.get_block_hash(number - 1),
///     which the verifier's seeded RecentBlockHashes answered with exactly
///     this value;
///   * DELIBERATE deviation: a failed top-level system call (contract code
///     present but reverted) is an error here, at block start as well as at
///     block end. Upstream asserts success (a no-op in release builds, i.e.
///     the failure was silently ignored); geth treats a failed system call
///     as an invalid block, and so does the verifier consuming this header.
///
/// Error model: storage READ errors follow EthereumState's fail-safe model
/// (the noexcept evmc::Host boundary reports a failed read as absent/empty —
/// the same model every transaction in the block already executes under, with
/// the post-execution state-root check as the backstop). Write-back
/// (applyToStorage) failures and EVM-level call failures are surfaced as the
/// returned error string, failing the block.

#pragma once

#include "EVMSupport.h"
#include "EthereumHost.h"
#include "EthereumState.h"
#include "bcos-task/Task.h"
#include <array>
#include <evmc/evmc.hpp>
#include <optional>
#include <string>
#include <vector>

namespace bcos::executor_v1::eth
{
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

/// `requests` object (ported evmone::state::Requests).
///
/// Defined by EIP-7685: General purpose execution layer requests.
/// https://eips.ethereum.org/EIPS/eip-7685.
struct EthRequests
{
    /// The type of the requests.
    enum class Type : uint8_t
    {
        /// Deposit requests.
        /// Introduced by EIP-6110 https://eips.ethereum.org/EIPS/eip-6110.
        deposit = 0,

        /// Withdrawal requests.
        /// Introduced by EIP-7002 https://eips.ethereum.org/EIPS/eip-7002.
        withdrawal = 1,

        /// Consolidation requests.
        /// Introduced by EIP-7251 https://eips.ethereum.org/EIPS/eip-7251.
        consolidation = 2,
    };

    /// Raw encoded data of requests object: first byte is type, the rest is request objects.
    bytes raw_data;

    explicit EthRequests(Type _type, bytes_view data = {})
    {
        raw_data.reserve(1 + data.size());
        raw_data += static_cast<uint8_t>(_type);
        raw_data += data;
    }

    /// Requests type.
    Type type() const noexcept { return static_cast<Type>(raw_data[0]); }

    /// Requests data - an opaque byte array, contains zero or more encoded request objects.
    bytes_view data() const noexcept { return {raw_data.data() + 1, raw_data.size() - 1}; }

    /// Append data to requests object byte array.
    void append(bytes_view data) { raw_data.append(data); }
};

namespace eth_system_calls_detail
{
/// Information about a registered "storage" system contract. They are executed at the block
/// start to store additional information in the State.
struct StorageSystemContract
{
    evmc_revision since;  ///< EVM revision in which added.
    address addr;         ///< Address of the system contract.
};

/// Registered "storage" system contracts, ordered by revision.
inline constexpr std::array STORAGE_SYSTEM_CONTRACTS{
    StorageSystemContract{EVMC_CANCUN, BEACON_ROOTS_ADDRESS},
    StorageSystemContract{EVMC_PRAGUE, HISTORY_STORAGE_ADDRESS},
};

/// Information about a registered "requests" system contract. They are executed at the block
/// end and produce requests: typed sequence of bytes.
struct RequestsSystemContract
{
    evmc_revision since;             ///< EVM revision in which added.
    address addr;                    ///< Address of the system contract.
    EthRequests::Type request_type;  ///< Type of requests produced.
};

/// Registered "requests" system contracts, ordered by revision.
inline constexpr std::array REQUESTS_SYSTEM_CONTRACTS{
    RequestsSystemContract{EVMC_PRAGUE, WITHDRAWAL_REQUEST_ADDRESS, EthRequests::Type::withdrawal},
    RequestsSystemContract{
        EVMC_PRAGUE, CONSOLIDATION_REQUEST_ADDRESS, EthRequests::Type::consolidation},
};

/// Executes one system call (ported evmone execute_system_call): a raw
/// EVMC_CALL from SYSTEM_ADDRESS run with vm.execute() directly — no nonce
/// bump, no value transfer, no recipient touch and no access-list warm-up,
/// exactly as upstream. The block-hash lookup is left empty: none of the
/// four system contracts executes BLOCKHASH (EIP-2935 receives the parent
/// hash as calldata instead).
template <class Storage>
evmc::Result executeSystemCall(EthereumState<Storage>& state, EthBlockInfo const& block,
    evmc_revision rev, evmc::VM& vm, address const& addr, bytes_view code, bytes_view input)
{
    // Every field is listed (the tree builds with -Werror
    // -Wmissing-field-initializers). value is zero — system calls transfer
    // nothing and charge nothing. code_address is zero like upstream's
    // message: the top-level code is passed to vm.execute() explicitly, so
    // nothing at depth 0 reads the field.
    const evmc_message msg{
        .kind = EVMC_CALL,
        .flags = 0,
        .depth = 0,
        .gas = 30'000'000,
        .recipient = addr,
        .sender = SYSTEM_ADDRESS,
        .input_data = input.data(),
        .input_size = input.size(),
        .value = {},
        .create2_salt = {},
        .code_address = {},
        .code = nullptr,
        .code_size = 0,
    };

    EthereumHost<Storage> host{rev, vm, state, block, /*blockHashLookup=*/{},
        /*tx=*/nullptr, EthCallParams{}, /*chainId=*/0};
    return vm.execute(host, rev, msg, code.data(), code.size());
}
}  // namespace eth_system_calls_detail

/// Block-start system calls (ported evmone system_call_block_start): EIP-4788
/// beacon-roots write (Cancun+) and EIP-2935 historical block-hash write
/// (Prague+), each gated by revision and skipped silently when the contract
/// has no code (per the EIPs). The contracts' state updates are written into
/// the view in place; returns an error string on failure (a failed call or a
/// failed write-back means divergent local state — an invalid block).
/// Call only when the block's revision >= EVMC_CANCUN.
///
/// @param parentBlockHash the EIP-2935 input (hash of block number - 1).
template <class Storage>
task::Task<std::optional<std::string>> systemCallBlockStart(Storage& view, evmc::VM& vm,
    EthBlockInfo const& block, evmc::bytes32 const& parentBlockHash, evmc_revision rev)
{
    EthereumState<Storage> state(view);
    try
    {
        for (const auto& contract : eth_system_calls_detail::STORAGE_SYSTEM_CONTRACTS)
        {
            if (rev < contract.since)
                break;  // Entries are ordered: no other contracts for this revision.

            // Skip the call if the target account doesn't exist. This is by EIP-4788 spec.
            // > if no code exists at [address], the call must fail silently.
            const auto code = state.get_code(contract.addr);
            if (code.empty())
                continue;

            // EIP-4788 stores the parent beacon block root; EIP-2935 stores the
            // parent block hash.
            const bytes32 input = contract.addr == HISTORY_STORAGE_ADDRESS ?
                                      parentBlockHash :
                                      block.parent_beacon_block_root;
            const auto res = eth_system_calls_detail::executeSystemCall(
                state, block, rev, vm, contract.addr, code, input);
            if (res.status_code != EVMC_SUCCESS)
            {
                co_return "block-start system call (EIP-4788/2935) failed: system contract "
                          "execution reverted";
            }
        }
        co_await state.applyToStorage(rev);
    }
    catch (std::exception const& e)
    {
        co_return std::string("block-start system call (EIP-4788/2935) failed: ") + e.what();
    }
    catch (...)
    {
        co_return "block-start system call (EIP-4788/2935) failed: unknown exception";
    }
    co_return std::nullopt;
}

struct EthBlockEndSystemCallsResult
{
    std::optional<std::string> error;
    std::vector<EthRequests> requests;  ///< EIP-7002/7251 requests (EIP-7685)
};

/// Block-end system calls (ported evmone system_call_block_end): EIP-7002
/// withdrawal requests and EIP-7251 consolidation requests (Prague+). A
/// missing contract code or a reverted call is an error — on a real chain
/// both contracts are deployed by ordinary pre-fork transactions, so a
/// failure means divergent local state.
/// Call only when the block's revision >= EVMC_PRAGUE.
template <class Storage>
task::Task<EthBlockEndSystemCallsResult> systemCallBlockEnd(
    Storage& view, evmc::VM& vm, EthBlockInfo const& block, evmc_revision rev)
{
    EthBlockEndSystemCallsResult result;
    EthereumState<Storage> state(view);
    try
    {
        for (const auto& contract : eth_system_calls_detail::REQUESTS_SYSTEM_CONTRACTS)
        {
            if (rev < contract.since)
                break;  // Entries are ordered: no other contracts for this revision.

            // Fail if the target account doesn't exist. This is by EIP-7002 and EIP-7251 spec.
            const auto code = state.get_code(contract.addr);
            if (code.empty())
            {
                result.error =
                    "block-end system call (EIP-7002/7251) failed: system contract code missing";
                co_return result;
            }

            const auto res =
                eth_system_calls_detail::executeSystemCall(state, block, rev, vm, contract.addr,
                    code, {});
            if (res.status_code != EVMC_SUCCESS)
            {
                result.error = "block-end system call (EIP-7002/7251) failed: execution reverted";
                co_return result;
            }
            result.requests.emplace_back(
                contract.request_type, bytes_view{res.output_data, res.output_size});
        }
        co_await state.applyToStorage(rev);
    }
    catch (std::exception const& e)
    {
        result.error = std::string("block-end system call (EIP-7002/7251) failed: ") + e.what();
    }
    catch (...)
    {
        result.error = "block-end system call (EIP-7002/7251) failed: unknown exception";
    }
    co_return result;
}
}  // namespace bcos::executor_v1::eth
