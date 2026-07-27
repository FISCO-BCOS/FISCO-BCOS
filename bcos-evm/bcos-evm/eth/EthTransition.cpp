#include <bcos-evm/adapter/StateDiffSanitize.h>
#include <bcos-evm/eth/EthTransition.h>

namespace bcos::evm::eth
{
Result runTransaction(const evmone::state::StateView& view, const evmone::state::BlockInfo& block,
    const evmone::state::BlockHashes& hashes, const evmone::state::Transaction& tx,
    evmc_revision rev, evmc::VM& vm, int64_t blockGasLeft, int64_t blobGasLeft)
{
    const auto validated =
        evmone::state::validate_transaction(view, block, tx, rev, blockGasLeft, blobGasLeft);
    if (const auto* err = std::get_if<std::error_code>(&validated))
        return *err;
    auto receipt = evmone::state::transition(view, block, hashes, tx, rev, vm,
        std::get<evmone::state::TransactionProperties>(validated));
    receipt.state_diff = bcos::evm::sanitizeStateDiff(view, std::move(receipt.state_diff));
    return receipt;
}

evmone::state::StateDiff runBlockFinalize(const evmone::state::StateView& view, evmc_revision rev,
    const evmc::address& coinbase, std::optional<uint64_t> blockReward,
    std::span<const evmone::state::Ommer> ommers,
    std::span<const evmone::state::Withdrawal> withdrawals)
{
    return bcos::evm::sanitizeStateDiff(
        view, evmone::state::finalize(view, rev, coinbase, blockReward, ommers, withdrawals));
}
}  // namespace bcos::evm::eth
