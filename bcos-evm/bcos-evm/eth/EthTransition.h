#pragma once

#include <bcos-evm/eth/state/state.hpp>
#include <evmc/evmc.hpp>
#include <optional>
#include <span>
#include <system_error>
#include <variant>

namespace bcos::evmref::eth
{
using Result = std::variant<evmone::state::TransactionReceipt, std::error_code>;

/// validate -> transition (spec §4.2). Zero fee logic: everything reuses evmone.
/// Does not write back state; the caller settles it via applyStateDiff(receipt.state_diff).
/// blobGasLeft and BlockInfo.blob_base_fee are precomputed by the caller from BlobParams
/// (spec §2).
[[nodiscard]] Result runTransaction(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    const evmone::state::Transaction& tx, evmc_revision rev, evmc::VM& vm, int64_t blockGasLeft,
    int64_t blobGasLeft);

/// Block-level finalization (withdrawals / ommer rewards). Returns the diff to be written back.
[[nodiscard]] evmone::state::StateDiff runBlockFinalize(const evmone::state::StateView& view,
    evmc_revision rev, const evmc::address& coinbase, std::optional<uint64_t> blockReward,
    std::span<const evmone::state::Ommer> ommers,
    std::span<const evmone::state::Withdrawal> withdrawals);
}  // namespace bcos::evmref::eth
