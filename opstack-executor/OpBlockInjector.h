// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once
// OP per-transaction injection loop production module (spec §7.0). Consumed directly at
// BaselineScheduler wiring.
#include <bcos-evm/adapter/StateDiffSanitize.h>
#include <bcos-evm/adapter/StateRootCompute.h>
#include <bcos-evm/opstack/OpFeeParams.h>  // v2（B1）：loadOpFeeParams / OpFeeParams
#include <bcos-evm/opstack/OpPredeploys.h>  // v2 (B1): OP_L1_BLOCK / OP_DEPOSITOR / OP_L2_TO_L1_MESSAGE_PASSER (not transitively reachable)
#include <ethereum-executor/BCOS2Evmone.h>  // applyStateDiff
#include <ethereum-executor/StorageStateView.h>
#include <opstack-executor/OpBlockExecute.h>  // v2 (B5): narrowGasUsed / hexCumulative / isL1AttributesTx exported from .cpp into this header (seal merged here)
#include <opstack-executor/OpEngineSeam.h>    // computeOpTxRoot / toBcosH256 declarations
#include <opstack-executor/OpErrors.h>
#include <opstack-executor/OpRlpDecode.h>  // toBlockInfo / narrowU256ToU64
#include <opstack-executor/OpSchedulerImpl.h>  // v2 (executability): OpExecuteBlockResult defined here (:66); missing it fails compilation
#include <opstack-executor/OpstackExecutor.h>
#include <opstack-executor/RecentBlockHashes.h>
#include <opstack-executor/Storage2State.h>
#include <bcos-evm/eth/state/system_contracts.hpp>

namespace bcos::evm::engine
{
/// Per-transaction injection loop: replicates processOpBlock's orchestration
/// (system_call_block_start → deposit-first → lazy loadOpFeeParams + Jovian D-1 override →
/// per-tx blockGasLeft decrement + setCumulativeGasUsed → finalizeBlock), executed through the
/// OpstackExecutor injection-style entry points. Returns the same result shape as processOpBlock.
/// Review D6: normal txs' FISCO Transactions are pre-built by the caller (normalTxs maps 1:1 to
/// the non-deposit txs) — opEnvelopeToTars lives in the engine lib; building it here would create
/// a link cycle.
/// Review R3: error classification — poison/hashErr → OpStorageError, block shape/validation →
/// OpConsensusError.
template <class Storage>
OpExecuteBlockResult runOpBlockInjection(bcos::executor_v1::opstack::OpstackExecutor& executor,
    Storage& view, bcos::protocol::BlockHeader const& header,
    std::span<bcos::evm::opstack::OpBlockTx const> txs,
    std::span<bcos::protocol::Transaction::Ptr const> normalTxs,
    bcos::evm::opstack::OpForkConfig const& cfg, uint64_t chainId,
    bcos::ledger::LedgerConfig const& ledgerConfig, std::vector<bcos::bytes> const& rawTxBytes,
    bcos::crypto::Hash::Ptr const& hashImpl)
{
    namespace detail = bcos::evm::engine::detail;
    namespace op = bcos::evm::opstack;
    namespace eth = bcos::executor_v1::eth;

    auto blk = detail::toBlockInfo(header);
    std::optional<std::string> hashErr;
    detail::RecentBlockHashes<Storage> hashes(
        view, blk.number, detail::toEvmcBytes32(header.parentInfo().blockHash), &hashErr);
    eth::StorageStateView<Storage> stateView(view);

    // (1) Pre-block system_call_block_start (no executor entry; evmone called directly).
    auto sysDiff =
        evmone::state::system_call_block_start(stateView, blk, hashes, cfg.rev, executor.vm());
    bcos::task::syncWait(eth::applyStateDiff(
        view, bcos::evm::sanitizeStateDiff(stateView, sysDiff), cfg.rev, *hashImpl));

    // (2) deposit-first content check + Jovian shape. Review R3: block-shape rejection →
    // OpConsensusError. Round-3 P3-7: call the exported op::isL1AttributesTx (exported to
    // OpBlockExecute.h) rather than inlining a copy — a duplicate would drift from processOpBlock.
    if (txs.empty())
        throw OpConsensusError("op block: missing L1 attributes deposit (empty block)");
    auto const* firstDep = std::get_if<op::DepositTx>(&txs[0].tx);
    if (firstDep == nullptr || !op::isL1AttributesTx(*firstDep))
        throw OpConsensusError("op block: first tx is not the L1 attributes deposit");
    op::validateJovianBlockShape(txs, cfg);

    op::OpBlockResult result;
    result.receipts.reserve(txs.size());
    int64_t blockGasLeft = blk.gas_limit;
    int64_t cumulative = 0;
    bool seenNonDeposit = false;
    bool feeLoaded = false;
    std::size_t normalIdx = 0;  // Review D6: consume caller-prebuilt normalTxs
    op::OpFeeParams fee{};
    for (std::size_t i = 0; i < txs.size(); ++i)
    {
        auto const& btx = txs[i];
        if (auto const* dep = std::get_if<op::DepositTx>(&btx.tx))
        {
            if (seenNonDeposit)
                throw OpConsensusError("op block: deposit after non-deposit tx");
            auto receipt = bcos::task::syncWait(executor.executeDeposit(
                view, header, *dep, chainId, blockGasLeft, ledgerConfig, &hashes));
            auto const gasUsed =
                op::narrowGasUsed(receipt->gasUsed());  // v2: op::-qualified
                                                        // (R3-exported to ns bcos::evm::opstack)
            blockGasLeft -= gasUsed;
            cumulative += gasUsed;
            receipt->setCumulativeGasUsed(op::hexCumulative(cumulative));  // v2: op::-qualified
            result.receipts.emplace_back(std::move(receipt));
            result.txTypes.emplace_back(static_cast<uint8_t>(op::kDepositTxType));
        }
        else
        {
            seenNonDeposit = true;
            if (!feeLoaded)
            {
                fee = op::loadOpFeeParams(stateView);
                if (cfg.has_da_footprint)
                {
                    auto const& attrData = std::get<op::DepositTx>(txs[0].tx).data;
                    if (attrData.size() == op::IsthmusL1AttributesLen)
                        fee.da_footprint_gas_scalar = 0;
                    else if (attrData.size() >= op::JovianL1AttributesLen)
                        fee.da_footprint_gas_scalar = static_cast<uint16_t>(
                            (static_cast<uint16_t>(attrData[op::JovianL1AttributesLen - 2]) << 8) |
                            static_cast<uint16_t>(attrData[op::JovianL1AttributesLen - 1]));
                }
                feeLoaded = true;
            }
            auto const& tx = std::get<evmone::state::Transaction>(btx.tx);
            // Review D6: normal txs are pre-built by the caller (normalTxs[i], whose
            // extraTransactionBytes is already the full envelope — spec §2, takeToTarsTransaction
            // stores the signing preimage and must be overwritten).
            // Final review I-2: normalIdx has no upper-bound guard; a short caller vector would
            // OOB (BaselineScheduler consumes this in production).
            if (normalIdx >= normalTxs.size())
                throw OpConsensusError(
                    "runOpBlockInjection: normalTxs exhausted (caller-provided "
                    "normal txs mismatch block txs)");
            // Review R3 classification contract: validation failure (opValidate rejects a normal
            // tx) → OpConsensusError (INVALID), not -32603. Before the Task 6 swap,
            // OpTxValidationFailed only appeared in the direct injector (the harness skips
            // invalid_ vectors); on the post-swap engine delegate path, misclassification would
            // report INVALID vectors as -32603 (mapDelegateError throws an internal error) —
            // aligned with processOpBlock's validate-error channel (runtime_error →
            // OpConsensusError). syncWait rethrows synchronously; FISCO types have stable
            // typeinfo, so typed catch binds reliably.
            protocol::TransactionReceipt::Ptr receipt;
            try
            {
                receipt = bcos::task::syncWait(executor.executeTransaction(view, header,
                    *normalTxs[normalIdx++], /*contextID=*/0, ledgerConfig,
                    /*call=*/false, fee, blockGasLeft, chainId, &hashes));
            }
            catch (const bcos::executor_v1::opstack::OpTxValidationFailed& e)
            {
                throw OpConsensusError(
                    "runOpBlockInjection: normal tx validation failed: " + std::string(e.what()));
            }
            auto const gasUsed = op::narrowGasUsed(receipt->gasUsed());  // v2: op::-qualified
            blockGasLeft -= gasUsed;
            cumulative += gasUsed;
            receipt->setCumulativeGasUsed(op::hexCumulative(cumulative));  // v2: op::-qualified
            result.receipts.emplace_back(std::move(receipt));
            result.txTypes.emplace_back(static_cast<uint8_t>(tx.type));
        }
    }
    bcos::task::syncWait(executor.finalizeBlock(view, header, ledgerConfig));
    result.gasUsed = cumulative;
    // Review R3: storage-layer failure (block-hash lookup / poison flag) → OpStorageError
    // (-32603), not INVALID.
    if (hashErr.has_value())
        throw OpStorageError("runOpBlockInjection: block-hash lookup failed: " + *hashErr);

    // (4) commitments: MessagePasser snapshot → seal → stateRoot → txRoot.
    std::map<evmc::bytes32, evmc::bytes32> mpStorage;
    bcos::evm::evmstate::Storage2State<Storage> bridge(view);
    bridge.visitAccounts([&](auto const& acc) {
        if (acc.addr == op::OP_L2_TO_L1_MESSAGE_PASSER)
        {
            mpStorage = acc.storage;
            return false;
        }
        return true;
    });
    if (bridge.poisoned())
        throw OpStorageError("runOpBlockInjection: poisoned: " + std::string(bridge.firstError()));
    auto seal = op::sealOpBlock(result, cfg, mpStorage);
    auto root = bcos::evm::stateRootOf(bridge);
    if (bridge.poisoned())
        throw OpStorageError(
            "runOpBlockInjection: poisoned after stateRootOf: " + std::string(bridge.firstError()));
    auto txRoot = computeOpTxRoot(rawTxBytes);
    return OpExecuteBlockResult{std::move(result.receipts), seal, detail::toBcosH256(root),
        static_cast<uint64_t>(cumulative), txRoot};
}
}  // namespace bcos::evm::engine
