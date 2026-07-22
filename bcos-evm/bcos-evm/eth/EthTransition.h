#pragma once

#include <bcos-evm/eth/state/state.hpp>
#include <evmc/evmc.hpp>
#include <optional>
#include <span>
#include <system_error>
#include <variant>

namespace bcos::evmref::eth
{
/// Result of executing a single transaction: a receipt on success, or an
/// error_code when validation fails (spec §4.2).
using Result = std::variant<evmone::state::TransactionReceipt, std::error_code>;

/// Execute a single transaction: validate -> transition (spec §4.2).
/// The zero-fee logic reuses evmone entirely.
///
/// This function does NOT write back to state; the caller persists the changes
/// via applyStateDiff(receipt.state_diff). blobGasLeft and BlockInfo.blob_base_fee
/// are pre-computed by the caller from BlobParams (spec §2).
///
/// @param view          Read-only pre-state view; the source for account/storage reads.
/// @param block         Block-level environment (coinbase, timestamp, gas limit,
///                      base_fee, blob_base_fee, prev_randao, ...) consumed by
///                      opcodes such as COINBASE/TIMESTAMP/BASEFEE and by gas metering.
/// @param hashes        Provider of historical block hashes for the BLOCKHASH opcode.
/// @param tx            The transaction to execute (from/to/value/data/gas/nonce/
///                      access list/blob hashes, ...).
/// @param rev           Target EVM revision (fork), selecting which EIP semantics
///                      and gas rules apply.
/// @param vm            The EVM instance (evmone) that runs the bytecode.
/// @param blockGasLeft  Gas remaining in the block (block gas limit minus already
///                      consumed); used to check the tx gas limit against the block.
/// @param blobGasLeft   Blob gas remaining in the block; EIP-4844 blob-tx budget check.
/// @return              A TransactionReceipt on success, or an error_code on
///                      validation failure.
[[nodiscard]] Result runTransaction(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    const evmone::state::Transaction& tx, evmc_revision rev, evmc::VM& vm, int64_t blockGasLeft,
    int64_t blobGasLeft);

/// Block-level finalization: settle withdrawals and ommer/block rewards after all
/// transactions in the block have executed. Pure computation — returns the diff to
/// be written back; the caller is responsible for persisting it.
///
/// @param view         Read-only pre-state view.
/// @param rev          Target EVM revision, selecting the settlement rules
///                     (e.g. withdrawals only exist from Shanghai onwards).
/// @param coinbase     Block producer address; recipient of block/ommer rewards.
/// @param blockReward  Base block reward; std::nullopt means no reward
///                     (e.g. zero post-merge). Used by the PoW reward model.
/// @param ommers       Ommer (uncle) blocks, for computing ommer rewards (pre-merge PoW).
/// @param withdrawals  EIP-4895 withdrawals (Shanghai+), credited to the target accounts.
/// @return             The StateDiff to write back.
[[nodiscard]] evmone::state::StateDiff runBlockFinalize(const evmone::state::StateView& view,
    evmc_revision rev, const evmc::address& coinbase, std::optional<uint64_t> blockReward,
    std::span<const evmone::state::Ommer> ommers,
    std::span<const evmone::state::Withdrawal> withdrawals);
}  // namespace bcos::evmref::eth
