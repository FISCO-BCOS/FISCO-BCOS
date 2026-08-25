#pragma once

#include <bcos-evm/adapter/StateDiffSanitize.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/OpTransition.h>  // DepositTx
#include <bcos-framework/engine/Types.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-utilities/BoostLog.h>  // BCOS_LOG (demoted-deposit-check observability)
#include <bcos-utilities/Common.h>
#include <opstack-executor/OpCommitments.h>  // OpBlockCommitments / payloadBloomToH2048
#include <opstack-executor/OpCommon.h>       // toBlockInfo / narrowU256ToU64 / toEvmcBytes32
#include <opstack-executor/OpstackExecutor.h>
#include <opstack-executor/RecentBlockHashes.h>
#include <opstack-executor/Storage2State.h>
#include <algorithm>
#include <array>
#include <bcos-evm/eth/state/system_contracts.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace bcos::evm::opstack
{
// ---- Jovian L1-attributes block shape ----
// The L1-attributes deposit's calldata is 176B on the Jovian activation block, 178B with the
// Jovian selector thereafter.
inline constexpr std::size_t IsthmusL1AttributesLen = 176;
inline constexpr std::size_t JovianL1AttributesLen = 178;
inline constexpr std::array<uint8_t, 4> JovianL1AttributesSelector = {0x3d, 0xb6, 0xbe, 0x2b};

/// Stricter-than-spec content check for the L1 attributes deposit (to==OP_L1_BLOCK &&
/// from==OP_DEPOSITOR); rejects hand-crafted payloads.
[[nodiscard]] inline bool isL1AttributesTx(const DepositTx& dep) noexcept
{
    return dep.to.has_value() && *dep.to == OP_L1_BLOCK && dep.from == OP_DEPOSITOR;
}
}  // namespace bcos::evm::opstack

// ---- block-pre steps for the scheduler execution path ----
//
// The block-level seal/finalize surface (processOpBlock / validateJovianBlockShape /
// finalizeOpBlock / opStorageRoot / sealOpBlock / encodeReceiptForRoot / finalizeOpBlockResult /
// computeOpTxRoot) is deferred to the part that delivers its definitions (part 4/5).

namespace bcos::evm::engine
{
/// Block-pre steps shared by the OpScheduler execution path (and previously the retired
/// runOpBlockInjection): recent-block-hashes construction → system_call_block_start +
/// write-back → deposit-first content check + Jovian shape → DA footprint gas scalar. Outputs
/// hashes (emplaced in place — RecentBlockHashes holds a storage reference, not assignable, hence
/// the std::optional carrier), hashErr, and daFootprintGasScalar via reference params. Throws
/// OpConsensusError on shape/validation faults.
template <class Storage, class RawTxRange>
void preBlockOpSteps(Storage& view, bcos::protocol::BlockHeader const& header,
    bcos::evm::opstack::OpForkConfig const& cfg, RawTxRange const& rawTxBytes,
    std::vector<bcos::evm::opstack::DepositTx> const& deposits,
    bcos::executor_v1::opstack::OpstackExecutor& executor,
    std::optional<detail::RecentBlockHashes<Storage>>& hashes, std::optional<std::string>& hashErr,
    std::optional<uint16_t>& daFootprintGasScalar)
{
    namespace op = bcos::evm::opstack;

    auto blk = detail::toBlockInfo(header);
    hashes.emplace(
        view, blk.number, detail::toEvmcBytes32(header.parentInfo().blockHash), &hashErr);
    bcos::evm::evmstate::Storage2State<Storage> stateView(view, executor.sharedError());

    // (1) Pre-block system call; write back through the bridge (the diff is sanitized first —
    // applyDiff's precondition). applyDiff poisons AND rethrows raw; a write-back failure is a
    // local storage fault, so it leaves as OpStorageError, never a bare runtime_error.
    auto sysDiff =
        evmone::state::system_call_block_start(stateView, blk, *hashes, cfg.rev, executor.vm());
    try
    {
        stateView.applyDiff(bcos::evm::sanitizeStateDiff(stateView, std::move(sysDiff)));
    }
    catch (const std::exception& e)
    {
        throw OpStorageError(std::string("pre-block system-call write-back failed: ") + e.what());
    }
    catch (...)
    {
        throw OpStorageError("pre-block system-call write-back failed: unknown exception");
    }
    // Read-path poison from the system call with applyDiff returning normally — same check as
    // the deposit/tx write-back paths in OpstackExecutor; without it a storage fault here would
    // silently execute as zero-value reads and surface later as a stateRoot mismatch.
    if (stateView.poisoned())
        throw OpStorageError("pre-block system-call poisoned: " + stateView.firstError());

    // (2) deposit-first content check + Jovian shape (type-byte classification, no raw-tx parse).
    constexpr uint8_t kDepositTypeByte = 0x7e;
    if (rawTxBytes.empty())
        throw OpConsensusError("op block: missing L1 attributes deposit (empty block)");
    // Empty-envelope guard: the first envelope must be non-empty before its type byte is read
    // (and before raw.back()[0] below). A block with NO deposit at all stays a hard reject: the
    // L1-attributes deposit seeds the block's fee/DA context and deposits[0] is read below.
    if (rawTxBytes[0].empty() || rawTxBytes[0][0] != kDepositTypeByte || deposits.empty())
        throw OpConsensusError("op block: no deposit transaction to seed the block");
    // L1-attributes content check — demoted from a hard reject to an observable log:
    // op-geth/op-reth parse the first tx as the L1 info (extract_l1_info) without validating it
    // is the L1-attributes tx, so both reference clients accept such a block.
    if (!op::isL1AttributesTx(deposits[0]))
        BCOS_LOG(WARNING) << LOG_BADGE("OP_BLOCK_EXEC")
                          << "op block: first tx is a deposit but not the L1 attributes tx — "
                             "accepted (deliberate demotion, op-geth/op-reth accept at "
                             "validation)";
    if (cfg.has_da_footprint)
    {
        auto const& data = deposits[0].data;
        if (data.size() == op::IsthmusL1AttributesLen)
        {
            // Jovian activation block must be deposits-only. Parity anchor: op-geth
            // CalcDAFootprint (core/types/rollup_cost.go:563-577, v1.101701.0) makes the exact
            // same last-tx-only check ("sufficient to check last transaction because deposits
            // precede non-deposit txs") and likewise relies on — without enforcing — the
            // deposit-prefix invariant, so with the deposit order gate demoted to a log our accept
            // set still matches op-geth's; iterating all envelopes here would be stricter than the
            // reference client and a consensus divergence.
            // Empty-envelope guard before back()[0] access (trailing empty tx -> same reject path).
            if (rawTxBytes.back().empty() || rawTxBytes.back()[0] != kDepositTypeByte)
                throw OpConsensusError(
                    "op block: unexpected non-deposit transactions in Jovian activation block");
        }
        else
        {
            if (data.size() < op::JovianL1AttributesLen)
                throw OpConsensusError(
                    "op block: L1 attributes transaction data too short for DA footprint gas "
                    "scalar");
            if (!std::equal(op::JovianL1AttributesSelector.begin(),
                    op::JovianL1AttributesSelector.end(), data.begin()))
                throw OpConsensusError(
                    "op block: L1 attributes transaction data does not have Jovian selector");
        }
    }

    // (3) DA scalar: Jovian extracts big-endian uint16 from deposits[0].data[176:178]; the
    // 176B Isthmus activation attributes → 0.
    if (cfg.has_da_footprint)
    {
        auto const& attrData = deposits[0].data;
        if (attrData.size() == op::IsthmusL1AttributesLen)
            daFootprintGasScalar = 0;
        else if (attrData.size() >= op::JovianL1AttributesLen)
            daFootprintGasScalar = static_cast<uint16_t>(
                (static_cast<uint16_t>(attrData[op::JovianL1AttributesLen - 2]) << 8) |
                static_cast<uint16_t>(attrData[op::JovianL1AttributesLen - 1]));
    }
}

/// Project the payload/header announced commitments into OpBlockCommitments (the "announced" side
/// of mismatchedFieldOf).
inline OpBlockCommitments announcedCommitmentsOf(const bcos::engine::ExecutionPayload& payload,
    const bcos::h256& transactionsRoot, const bcos::protocol::BlockHeader& ethHeader)
{
    // Isthmus+ payloads always carry withdrawalsRoot (upstream invariant), but if that ever lapses
    // the unconditional deref below would throw bad_optional_access (-> UnknownError). Guard it
    // into a clean consensus-level rejection naming the field (symmetric to mismatchedFieldOf's
    // report).
    if (!payload.withdrawalsRoot.has_value())
        throw OpConsensusError("op block: payload missing withdrawalsRoot");
    OpBlockCommitments out{
        .receiptsRoot = payload.receiptsRoot,
        .logsBloom = payloadBloomToH2048(payload.logsBloom),
        .withdrawalsRoot = *payload.withdrawalsRoot,
        .stateRoot = payload.stateRoot,
        .gasUsed = payload.gasUsed,
        .txRoot = transactionsRoot,
        .blobGasUsed = payload.blobGasUsed.has_value() ?
                           std::optional<uint64_t>(bcos::evm::engine::detail::narrowU256ToU64(
                               *payload.blobGasUsed, "ExecutionPayload.blobGasUsed")) :
                           std::nullopt,
        .requestsHash = ethHeader.requestsHash(),
    };
    return out;
}
}  // namespace bcos::evm::engine
