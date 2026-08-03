#include <bcos-evm/adapter/StateDiffSanitize.h>
#include <bcos-evm/opstack/OpBlockExecute.h>
#include <bcos-evm/opstack/OpBlockFinalize.h>
#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <bcos-evm/opstack/OpValidate.h>
#include <algorithm>
#include <bcos-evm/eth/state/system_contracts.hpp>
#include <stdexcept>

namespace bcos::evm::opstack
{
namespace
{
[[nodiscard]] bool isL1AttributesTx(const DepositTx& dep) noexcept
{
    // stricter-than-spec (spec §6 decision point 2, user ruling): validate by content. Spec
    // constants for cross-checking against op-node derive/l1_block_info.go:40 (DEPOSITOR); op-geth
    // EL does not perform this validation (responsibility pushed down to the CL layer).
    //
    // Impact assessment (2026-08-03 audit D3): this check is unreachable on a real chain —
    // op-node's attributes.go:188-190 unconditionally prepends the L1-info deposit
    // (`From=0xdead...0001`, `To=L1BlockAddr`, l1_block_info.go:572-573), which exactly satisfies
    // both conditions, with all upgrade txs (incl. Jovian L1Block deployment) following it. The
    // only way to trigger a DIVERGENT verdict (FISCO INVALID where op-geth would execute) is a
    // hand-crafted payload fed directly to engine_newPayload, bypassing op-node. Keep this stricter
    // check: it rejects malformed blocks op-geth accepts, at zero cost on legitimate payloads.
    return dep.to.has_value() && *dep.to == OP_L1_BLOCK && dep.from == OP_DEPOSITOR;
}
}  // namespace

void validateJovianBlockShape(std::span<const OpBlockTx> txs, const OpForkConfig& cfg)
{
    // C-3/C-4 are Jovian-only: op-geth calls CalcDAFootprint only under `IsJovian`
    // (block_validator.go:120) and CalcDAFootprint is documented "must not be called for pre-Jovian
    // blocks" (rollup_cost.go:562). `has_da_footprint` is true iff the active config is Jovian
    // (OpForkSchedule.cpp).
    if (!cfg.has_da_footprint)
        return;
    // The empty-block and non-deposit-first-tx cases are rejected by processOpBlock's own checks
    // (and by op-geth's `len(txs) == 0 || !txs[0].IsDepositTx()` guard). Return here rather than
    // duplicate that verdict, so this function's contract is exactly "the Jovian attributes shape".
    if (txs.empty())
        return;
    const auto* firstDep = std::get_if<DepositTx>(&txs[0].tx);
    if (firstDep == nullptr)
        return;

    const auto& data = firstDep->data;
    if (data.size() == IsthmusL1AttributesLen)
    {
        // C-4: Jovian *activation* block. The first Jovian block still carries Isthmus-length L1
        // attributes (no DA-footprint gas scalar yet) and op-geth requires it to be deposits-only
        // (rollup_cost.go:568-576). Checking the LAST tx is sufficient because deposits always
        // precede non-deposits (enforced by processOpBlock's "deposit after non-deposit" guard),
        // exactly op-geth's own justification.
        if (!std::holds_alternative<DepositTx>(txs.back().tx))
            throw std::runtime_error(
                "op block: unexpected non-deposit transactions in Jovian activation block");
        return;
    }

    // C-3: a normal Jovian block's L1 attributes calldata must carry the Jovian selector and be at
    // least the Jovian length (op-geth `ExtractDAFootprintGasScalar`, rollup_cost.go:547-556).
    if (data.size() < JovianL1AttributesLen)
        throw std::runtime_error(
            "op block: L1 attributes transaction data too short for DA footprint gas scalar");
    if (!std::equal(
            JovianL1AttributesSelector.begin(), JovianL1AttributesSelector.end(), data.begin()))
        throw std::runtime_error(
            "op block: L1 attributes transaction data does not have Jovian selector");
}

OpBlockResult processOpBlock(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    std::span<const OpBlockTx> txs, const OpForkConfig& cfg, evmc::VM& vm, uint64_t chainId,
    const std::function<void(const evmone::state::StateDiff&)>& applyDiff)
{
    // §4.1 step 1: pre-block system call (4788/2935; revision-gating and silent skip on missing
    // code are both handled inside evmone).
    applyDiff(bcos::evm::sanitizeStateDiff(
        view, evmone::state::system_call_block_start(view, block, hashes, cfg.rev, vm)));

    // §4.1 step 2 precondition: the first tx must be the L1 attributes deposit (stricter-than-spec,
    // spec §6 decision point 1/2).
    if (txs.empty())
        throw std::runtime_error("op block: missing L1 attributes deposit (empty block)");
    const auto* firstDep = std::get_if<DepositTx>(&txs[0].tx);
    if (firstDep == nullptr || !isL1AttributesTx(*firstDep))
        throw std::runtime_error("op block: first tx is not the L1 attributes deposit");

    // §4.1 step 2 precondition (batch C, spec §6.4): Jovian L1-attributes block shape — attributes
    // selector/length (C-3) and the activation block's deposits-only rule (C-4). No-op pre-Jovian.
    // Placed before the per-tx loop so an activation block carrying a user tx is refused before any
    // of it executes, mirroring op-geth's `CalcDAFootprint` in block validation.
    validateJovianBlockShape(txs, cfg);

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
                // D-1 (spec §6.4): Jovian DA footprint gas scalar 的权威来源是首笔 L1 attributes
                // deposit 的 calldata[176:178]（op-geth ExtractDAFootprintGasScalar,
                // rollup_cost.go:555），不是 L1Block slot8 —— attributes deposit 失败时
                // EVM 回滚存储写入, slot8 保持上一块旧值而 calldata 始终携带本块正确值。
                // 激活块 (data.size()==176) 强制 0 (op-geth CalcDAFootprint:571-577)；
                // 正常块 (data.size()>=178) 取固定偏移 [176:178] (非 len-2)。
                if (cfg.has_da_footprint)
                {
                    const auto& attrData = std::get<DepositTx>(txs[0].tx).data;
                    if (attrData.size() == IsthmusL1AttributesLen)
                    {
                        // C-4 下激活块无用户 tx, 本分支结构不可达; 若未来放宽 C-4, op-geth
                        // 在此会拒块 (CalcDAFootprint 对 Isthmus 长度要求 deposits-only,
                        // rollup_cost.go:572-575), 非置 0。
                        fee.da_footprint_gas_scalar = 0;
                    }
                    else if (attrData.size() >= JovianL1AttributesLen)
                    {
                        fee.da_footprint_gas_scalar = static_cast<uint16_t>(
                            (static_cast<uint16_t>(attrData[JovianL1AttributesLen - 2]) << 8) |
                            static_cast<uint16_t>(attrData[JovianL1AttributesLen - 1]));
                    }
                }
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
}  // namespace bcos::evm::opstack
