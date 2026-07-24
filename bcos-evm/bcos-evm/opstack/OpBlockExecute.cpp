#include <bcos-evm/adapter/StateDiffSanitize.h>
#include <bcos-evm/opstack/OpBlockExecute.h>
#include <bcos-evm/opstack/OpBlockFinalize.h>
#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <bcos-evm/opstack/OpValidate.h>
#include <bcos-evm/eth/state/system_contracts.hpp>
#include <stdexcept>

namespace bcos::evmref::opstack
{
namespace
{
[[nodiscard]] bool isL1AttributesTx(const DepositTx& dep) noexcept
{
    // stricter-than-spec (spec §6 decision point 2, user ruling): validate by content. Spec
    // constants for cross-checking against op-node derive/l1_block_info.go:40 (DEPOSITOR); op-geth
    // EL does not perform this validation (responsibility pushed down to the CL layer).
    return dep.to.has_value() && *dep.to == OP_L1_BLOCK && dep.from == OP_DEPOSITOR;
}
}  // namespace

OpBlockResult processOpBlock(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    std::span<const OpBlockTx> txs, const OpForkConfig& cfg, evmc::VM& vm, uint64_t chainId,
    const std::function<void(const evmone::state::StateDiff&)>& applyDiff)
{
    // §4.1 step 1: pre-block system call (4788/2935; revision-gating and silent skip on missing
    // code are both handled inside evmone).
    applyDiff(bcos::evmref::sanitizeStateDiff(
        view, evmone::state::system_call_block_start(view, block, hashes, cfg.rev, vm)));

    // §4.1 step 2 precondition: the first tx must be the L1 attributes deposit (stricter-than-spec,
    // spec §6 decision point 1/2).
    if (txs.empty())
        throw std::runtime_error("op block: missing L1 attributes deposit (empty block)");
    const auto* firstDep = std::get_if<DepositTx>(&txs[0].tx);
    if (firstDep == nullptr || !isL1AttributesTx(*firstDep))
        throw std::runtime_error("op block: first tx is not the L1 attributes deposit");

    OpBlockResult result;
    result.receipts.reserve(txs.size());
    int64_t blockGasLeft = block.gas_limit;
    int64_t cumulative = 0;
    bool seenNonDeposit = false;
    bool feeLoaded = false;
    OpFeeParams fee{};

    for (const auto& btx : txs)
    {
        if (const auto* dep = std::get_if<DepositTx>(&btx.tx))
        {
            if (seenNonDeposit)
                throw std::runtime_error("op block: deposit after non-deposit tx");
            auto receipt = runDeposit(view, block, hashes, *dep, cfg, vm, chainId, blockGasLeft);
            applyDiff(receipt.receipt.state_diff);
            blockGasLeft -= receipt.receipt.gas_used;
            cumulative += receipt.receipt.gas_used;
            receipt.receipt.cumulative_gas_used = cumulative;
            result.receipts.emplace_back(std::move(receipt));
        }
        else
        {
            seenNonDeposit = true;
            if (!feeLoaded)
            {
                // §4.1 step 2: fee params are read from the storage slot values after this block's
                // attributes tx executes; deferred lazily to the first normal tx, equivalent to
                // op-geth's per-block cache (rollup_cost.go:162-164/:199-207).
                fee = loadOpFeeParams(view);
                feeLoaded = true;
            }
            const auto& tx = std::get<evmone::state::Transaction>(btx.tx);
            const evmc::bytes_view env{btx.signedEnvelope.data(), btx.signedEnvelope.size()};
            auto v = opValidate(view, block, tx, env, cfg, fee, blockGasLeft);
            if (const auto* err = std::get_if<std::error_code>(&v))
                // op-geth: a normal tx that fails validation has no failed-receipt mechanism;
                // Process voids the whole block outright (state_transition preCheck →
                // state_processor.go:109-113).
                throw std::runtime_error("op block: invalid non-deposit tx: " + err->message());
            auto receipt = opTransition(
                view, block, hashes, tx, cfg, vm, std::get<OpTxProperties>(v), chainId, env);
            applyDiff(receipt.receipt.state_diff);
            blockGasLeft -= receipt.receipt.gas_used;
            cumulative += receipt.receipt.gas_used;
            receipt.receipt.cumulative_gas_used = cumulative;
            result.receipts.emplace_back(std::move(receipt));
        }
    }

    // §4.1 step 4: end-of-block finalize (D-10 wiring closure point).
    result.finalizeDiff = finalizeOpBlock(view, cfg, block.coinbase);
    applyDiff(result.finalizeDiff);


    result.gasUsed = cumulative;
    return result;
}
}  // namespace bcos::evmref::opstack
