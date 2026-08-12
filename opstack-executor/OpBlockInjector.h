// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once
// OP 逐笔注入循环生产模块（spec §7.0）。BaselineScheduler 接线时直接消费。
#include <bcos-evm/adapter/StateDiffSanitize.h>
#include <bcos-evm/adapter/StateRootCompute.h>
#include <bcos-evm/opstack/OpFeeParams.h>  // v2（B1）：loadOpFeeParams / OpFeeParams
#include <bcos-evm/opstack/OpPredeploys.h>  // v2（B1）：OP_L1_BLOCK / OP_DEPOSITOR / OP_L2_TO_L1_MESSAGE_PASSER（非传递可达）
#include <ethereum-executor/BCOS2Evmone.h>  // applyStateDiff
#include <ethereum-executor/StorageStateView.h>
#include <opstack-executor/OpBlockExecute.h>  // v2（B5）：narrowGasUsed / hexCumulative / isL1AttributesTx 由本任务从 .cpp 导出到本头
#include <opstack-executor/OpBlockSeal.h>
#include <opstack-executor/OpEngineSeam.h>  // computeOpTxRoot / toBcosH256 声明所在
#include <opstack-executor/OpErrors.h>
#include <opstack-executor/OpRlpDecode.h>  // toBlockInfo / narrowU256ToU64
#include <opstack-executor/OpSchedulerImpl.h>  // v2（可执行性）：OpExecuteBlockResult 定义在此（:66），缺则编译失败
#include <opstack-executor/OpstackExecutor.h>
#include <opstack-executor/RecentBlockHashes.h>
#include <opstack-executor/Storage2State.h>
#include <bcos-evm/eth/state/system_contracts.hpp>

namespace bcos::evm::engine
{
/// 逐笔注入循环：复刻 processOpBlock 的编排（system_call_block_start → deposit-first →
/// 懒 loadOpFeeParams + Jovian D-1 覆盖 → 逐笔 blockGasLeft 递减 + setCumulativeGasUsed →
/// finalizeBlock），经 OpstackExecutor 注入式入口执行。返回与 executeOpBlock 同形的结果。
/// 审查 D6：普通交易的 FISCO Transaction 由**调用方**预构建（normalTxs 与 txs 中普通交易
/// 一一对应）——opEnvelopeToTars 在 engine lib，injector 内建会成链接循环。
/// 审查 R3：错误分类 poison/hashErr → OpStorageError，块形状/校验 → OpConsensusError。
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

    // (1) 块前 system_call_block_start（executor 无入口，evmone 直调）。
    auto sysDiff =
        evmone::state::system_call_block_start(stateView, blk, hashes, cfg.rev, executor.vm());
    bcos::task::syncWait(eth::applyStateDiff(
        view, bcos::evm::sanitizeStateDiff(stateView, sysDiff), cfg.rev, *hashImpl));

    // (2) deposit-first 内容检查 + Jovian 形状。审查 R3：块形状拒绝 → OpConsensusError。
    // 第三轮 P3-7：直接调导出的 op::isL1AttributesTx（R3 导出到 OpBlockExecute.h），
    // 不内联重写（否则与 processOpBlock 形成第 2 份拷贝，违背"免复制漂移"）。
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
    std::size_t normalIdx = 0;  // 审查 D6：消费调用方预构建的 normalTxs
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
            auto const gasUsed = op::narrowGasUsed(receipt->gasUsed());  // v2：op:: 限定（R3 导出到
                                                                         // ns bcos::evm::opstack）
            blockGasLeft -= gasUsed;
            cumulative += gasUsed;
            receipt->setCumulativeGasUsed(op::hexCumulative(cumulative));  // v2：op:: 限定
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
            // 审查 D6：普通交易由调用方预构建（normalTxs[i]，其 extraTransactionBytes 已是完整
            // envelope——见 spec §2，takeToTarsTransaction 存的是 signing preimage 需覆盖）。
            // 终审 I-2：normalIdx 无上界守卫，调用方少传即 OOB（生产模块 BaselineScheduler
            // 将来消费）。
            if (normalIdx >= normalTxs.size())
                throw OpConsensusError(
                    "runOpBlockInjection: normalTxs exhausted (caller-provided "
                    "normal txs mismatch block txs)");
            auto receipt = bcos::task::syncWait(executor.executeTransaction(view, header,
                *normalTxs[normalIdx++], /*contextID=*/0, ledgerConfig,
                /*call=*/false, fee, blockGasLeft, chainId, &hashes));
            auto const gasUsed = op::narrowGasUsed(receipt->gasUsed());  // v2：op:: 限定
            blockGasLeft -= gasUsed;
            cumulative += gasUsed;
            receipt->setCumulativeGasUsed(op::hexCumulative(cumulative));  // v2：op:: 限定
            result.receipts.emplace_back(std::move(receipt));
            result.txTypes.emplace_back(static_cast<uint8_t>(tx.type));
        }
    }
    bcos::task::syncWait(executor.finalizeBlock(view, header, ledgerConfig));
    result.gasUsed = cumulative;
    // 审查 R3：存储层故障（block-hash 查找 / 毒标记）→ OpStorageError（-32603），非 INVALID。
    if (hashErr.has_value())
        throw OpStorageError("runOpBlockInjection: block-hash lookup failed: " + *hashErr);

    // (4) commitment：MessagePasser 快照 → seal → stateRoot → txRoot。
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
