#include <bcos-evm/adapter/StateDiffSanitize.h>
#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <opstack-executor/OpBlockExecute.h>
#include <opstack-executor/OpBlockFinalize.h>
#include <algorithm>
#include <bcos-evm/eth/state/system_contracts.hpp>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace bcos::evm::opstack
{
namespace
{
[[nodiscard]] bool isL1AttributesTx(const DepositTx& dep) noexcept
{
    // Stricter-than-spec: validate the L1 attributes deposit by content. op-geth's EL does not
    // perform this validation (pushed down to the CL layer); op-node always prepends a deposit
    // that satisfies both conditions, so a divergent verdict is only reachable via a hand-crafted
    // payload fed directly to engine_newPayload. Keep the check: it rejects malformed blocks
    // op-geth accepts, at zero cost on legitimate payloads.
    return dep.to.has_value() && *dep.to == OP_L1_BLOCK && dep.from == OP_DEPOSITOR;
}

/// Narrow the FISCO receipt's gasUsed (u256) back to the int64 the gas pool/cumulative accounting
/// uses. The execution layer only ever stores a small positive gas_used, but the "widen -> check ->
/// narrow" discipline (Storage2Ledger.h precedent) applies: a corrupt receipt must not silently
/// wrap blockGasLeft/cumulative.
[[nodiscard]] int64_t narrowGasUsed(const bcos::u256& gasUsed)
{
    static const bcos::u256 kMaxInt64(std::numeric_limits<int64_t>::max());
    if (gasUsed > kMaxInt64)
        throw std::runtime_error("op block: receipt gasUsed exceeds int64_t range");
    return static_cast<int64_t>(gasUsed);
}

/// "0x" + lowercase hex, no leading zeros (op-geth hexutil.Uint64 convention). Stored on the
/// receipt's cumulativeGasUsed string field; encodeReceiptForRoot parses it back to the exact
/// uint64 for the EncodeIndex leaf (see OpBlockSeal.cpp).
[[nodiscard]] std::string hexCumulative(uint64_t cumulative)
{
    std::ostringstream oss;
    oss << "0x" << std::hex << cumulative;
    return oss.str();
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
    const bcos::protocol::TransactionReceiptFactory::Ptr& receiptFactory,
    const std::function<void(const evmone::state::StateDiff&)>& applyDiff)
{
    // Step 1: pre-block system call (4788/2935; revision-gating and silent skip on missing code
    // are both handled inside evmone).
    applyDiff(bcos::evm::sanitizeStateDiff(
        view, evmone::state::system_call_block_start(view, block, hashes, cfg.rev, vm)));

    // Step 2 precondition: the first tx must be the L1 attributes deposit (stricter-than-spec).
    if (txs.empty())
        throw std::runtime_error("op block: missing L1 attributes deposit (empty block)");
    const auto* firstDep = std::get_if<DepositTx>(&txs[0].tx);
    if (firstDep == nullptr || !isL1AttributesTx(*firstDep))
        throw std::runtime_error("op block: first tx is not the L1 attributes deposit");

    // Step 2 precondition: Jovian L1-attributes block shape — attributes selector/length (C-3)
    // and the activation block's deposits-only rule (C-4). No-op pre-Jovian. Placed before the
    // per-tx loop so an activation block carrying a user tx is refused before any of it executes,
    // mirroring op-geth's `CalcDAFootprint` in block validation.
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
            evmone::state::StateDiff diff;
            auto receipt = runDeposit(
                view, block, hashes, *dep, cfg, vm, chainId, blockGasLeft, receiptFactory, diff);
            applyDiff(diff);
            const auto gasUsed = narrowGasUsed(receipt->gasUsed());
            blockGasLeft -= gasUsed;
            cumulative += gasUsed;
            receipt->setCumulativeGasUsed(hexCumulative(cumulative));
            result.receipts.emplace_back(std::move(receipt));
            result.txTypes.emplace_back(static_cast<uint8_t>(kDepositTxType));
        }
        else
        {
            seenNonDeposit = true;
            if (!feeLoaded)
            {
                // Step 2: fee params are read from the storage slot values after this block's
                // attributes tx executes; deferred lazily to the first normal tx, equivalent to
                // op-geth's per-block cache (rollup_cost.go:162-164/:199-207).
                fee = loadOpFeeParams(view);
                // The Jovian DA footprint gas scalar's authoritative source is the first L1
                // attributes deposit's calldata[176:178] (op-geth ExtractDAFootprintGasScalar,
                // rollup_cost.go:555), not L1Block slot8: if the attributes deposit fails, the EVM
                // rolls back the storage write so slot8 keeps the previous block's stale value,
                // while calldata always carries this block's correct value. An activation block
                // (data.size()==176) forces 0 (op-geth CalcDAFootprint:571-577); a normal block
                // (data.size()>=178) reads the fixed offset [176:178] (not len-2).
                if (cfg.has_da_footprint)
                {
                    const auto& attrData = std::get<DepositTx>(txs[0].tx).data;
                    if (attrData.size() == IsthmusL1AttributesLen)
                    {
                        // Under C-4 the activation block has no user tx, so this branch is
                        // structurally unreachable; if C-4 were ever relaxed, op-geth would reject
                        // the block here (CalcDAFootprint requires deposits-only for Isthmus
                        // length, rollup_cost.go:572-575) rather than set 0.
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
            // L1/DA/operator cost were derived from `env` in opValidate and frozen into props;
            // opTransition charges from that same snapshot (no second fee read, no re-encoding).
            evmone::state::StateDiff diff;
            auto receipt = opTransition(view, block, hashes, tx, cfg, vm,
                std::get<OpTxProperties>(v), chainId, receiptFactory, diff);
            applyDiff(diff);
            const auto gasUsed = narrowGasUsed(receipt->gasUsed());
            blockGasLeft -= gasUsed;
            cumulative += gasUsed;
            receipt->setCumulativeGasUsed(hexCumulative(cumulative));
            result.receipts.emplace_back(std::move(receipt));
            result.txTypes.emplace_back(static_cast<uint8_t>(tx.type));
        }
    }

    // Step 4: end-of-block finalize.
    result.finalizeDiff = finalizeOpBlock(view, cfg, block.coinbase);
    applyDiff(result.finalizeDiff);

    result.gasUsed = cumulative;
    return result;
}
}  // namespace bcos::evm::opstack
