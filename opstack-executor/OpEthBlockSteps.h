/// @file OpEthBlockSteps.h
/// @brief The block-level pre/post steps of the OP production execution paths
///        (OpScheduler::execute and OpBlockVerifier::verifyAndCommit) on the
///        bcos-evm-free layer — the counterparts of OpBlockExecute.h's
///        preBlockOpSteps / finalizeOpBlockResult, composed from
///        OpEthBlockExecute.h / OpEthCommitments.h / ethereum-executor
///        primitives and driven around OpEthExecutor + SchedulerSerialImpl.
///
/// Error classification is byte-compatible with the legacy pair so the
/// callers' catch ladders and reject surfaces are unchanged:
///   * block-content / shape faults -> bcos::evm::OpConsensusError (INVALID);
///     the legacy message texts are kept verbatim where the check lives here
///     (deposit-first, Jovian shape), while faults raised inside the new-layer
///     helpers (OpEthBlockError from buildOpEthBlockInfo / sealOpEthBlock) are
///     reclassified to OpConsensusError keeping the helper's message.
///   * storage / IO faults -> bcos::evm::engine::OpStorageError (-32603),
///     including a poisoned shared OpStorageErrorSlot (the fail-loud channel
///     every per-tx storage chain records into).
///
/// BLOCKHASH: the per-block OpRecentBlockHashes (op-geth GetHashFn semantics —
/// seed {N-1: parentHash}, lazy SYS_NUMBER_2_HASH walk-back) is the source;
/// it is wrapped into the new layer's eth::BlockHashLookup function by
/// opEthBlockHashLookup() so the EVM-visible answers are byte-identical to the
/// retired bcos-evm/adapter RecentBlockHashes path (3.5 ported the class here
/// as OpRecentBlockHashes, dropping the evmone BlockHashes base the adapter
/// needed and this layer never consumed).

#pragma once

#include <opstack-executor/OpCommon.h>  // OpConsensusError / OpStorageError / detail conversions
#include <opstack-executor/OpEthBlockExecute.h>  // buildOpEthBlockInfo / sealOpEthBlock / ...
#include <opstack-executor/OpEthCommitments.h>   // OpEthExecuteBlockResult / computeOpEthTransactionsRoot
#include <opstack-executor/OpEthDeposit.h>       // DepositTx / OP_DEPOSIT_TX_TYPE
#include <opstack-executor/OpExecutionPolicy.h>  // OpPolicy
#include <opstack-executor/OpStorageErrorGuard.h>  // OpStorageErrorSlot / OpFaultRecordingStorage
#include <opstack-executor/OpRecentBlockHashes.h>  // per-block BLOCKHASH source
#include <ethereum-executor/EthSystemCalls.h>    // eth::systemCallBlockStart
#include <ethereum-executor/EthereumTransition.h>  // eth::finalizeState
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-framework/protocol/TransactionReceiptNormalize.h>
#include <bcos-framework/storage2/RollbackableStorage.h>
#include <bcos-ledger/mpt/Constants.h>   // emptyRootHash
#include <bcos-ledger/mpt/StateRoots.h>  // computeMptStateRoot
#include <bcos-task/Task.h>
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/Common.h>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace bcos::executor_v1::opstack
{
namespace eth = bcos::executor_v1::eth;

/// Adapt the per-block OpRecentBlockHashes into the new layer's BlockHashLookup
/// function. The second (currentHeight) parameter is unused: OpRecentBlockHashes
/// already bounds answers to n < its construction height, and the EVM opcode
/// layer enforces the 256-block window — identical to the legacy path, which
/// passed the RecentBlockHashes object itself as the evmone BlockHashes.
template <class Storage>
[[nodiscard]] eth::BlockHashLookup opEthBlockHashLookup(
    OpRecentBlockHashes<Storage> const& hashes)
{
    return [&hashes](int64_t blockNumber, int64_t /*currentHeight*/) {
        return hashes.get_block_hash(blockNumber);
    };
}

/// Block-pre steps shared by OpScheduler::execute and
/// OpBlockVerifier::verifyAndCommit (mirror of the retired OpBlockExecute.h's
/// preBlockOpSteps): block-info projection → recent-block-hashes construction →
/// block-start system calls (Cancun+) → deposit-first content check + Jovian
/// shape → DA footprint gas scalar. Outputs @p hashes (emplaced in place —
/// OpRecentBlockHashes holds a storage reference, not assignable, hence the
/// std::optional carrier), @p hashErr and @p daFootprintGasScalar via reference
/// params (the caller co_awaits immediately; the referents live in its frame).
/// Throws OpConsensusError on shape/validation faults, OpStorageError on
/// storage faults (incl. a poisoned @p errorSlot from the system-call reads).
template <class Storage, class RawTxRange>
task::Task<void> preBlockOpEthSteps(Storage& view, bcos::protocol::BlockHeader const& header,
    OpForkSpec const& spec, RawTxRange const& rawTxBytes, std::vector<DepositTx> const& deposits,
    evmc::VM& vm, std::shared_ptr<OpStorageErrorSlot> const& errorSlot,
    std::optional<OpRecentBlockHashes<Storage>>& hashes,
    std::optional<std::string>& hashErr, std::optional<uint16_t>& daFootprintGasScalar)
{
    // buildOpEthBlockInfo's leniency rule is the legacy toBlockInfo one
    // (pre-Ecotone optionals zero-filled — dead EVM inputs pre-Cancun);
    // OpEthBlockError here is a header-content reject, classified INVALID like
    // the legacy requireHeaderField/narrowU256ToI64 OpConsensusErrors.
    eth::EthBlockInfo blk;
    try
    {
        blk = buildOpEthBlockInfo(header, spec);
    }
    catch (OpEthBlockError const& e)
    {
        throw bcos::evm::OpConsensusError(e.what());
    }
    hashes.emplace(view, blk.number,
        bcos::evm::engine::detail::toEvmcBytes32(header.parentInfo().blockHash), &hashErr);

    // (1) Block-start system calls (EIP-4788/2935), Cancun+ only — the gate is
    // the new layer's contract (eth::systemCallBlockStart), semantics-identical
    // to evmone's revision-gated no-op. The fault recorder routes every
    // storage2 fault into the block-wide slot (EthereumState swallows read
    // faults downstream, so the slot is the only place they stay visible); a
    // write-back/call failure arrives as the returned error string. Both leave
    // as OpStorageError, never a bare runtime_error — the legacy applyDiff
    // wrapping and post-write-back poison check.
    if (spec.rev >= EVMC_CANCUN)
    {
        evmc::bytes32 parentHash{};
        auto const& parent = header.parentInfo();
        std::copy_n(parent.blockHash.data(), sizeof(parentHash.bytes), parentHash.bytes);
        OpFaultRecordingStorage<Storage> faultProbe(view, errorSlot);
        OpPolicy const sysPolicy{spec, blk};
        auto const sysErr =
            co_await eth::systemCallBlockStart<OpFaultRecordingStorage<Storage>, OpPolicy>(
                faultProbe, vm, blk, parentHash, spec.rev, sysPolicy);
        if (sysErr)
        {
            throw bcos::evm::engine::OpStorageError(
                "pre-block system-call write-back failed: " + *sysErr);
        }
        if (errorSlot && errorSlot->poisoned())
        {
            throw bcos::evm::engine::OpStorageError(
                "pre-block system-call poisoned: " + errorSlot->firstErrorMessage());
        }
    }

    // (2) Deposit-first content check + Jovian shape (type-byte classification,
    // no raw-tx parse). Messages kept verbatim from preBlockOpSteps.
    if (rawTxBytes.empty())
        throw bcos::evm::OpConsensusError("op block: missing L1 attributes deposit (empty block)");
    // Empty-envelope guard: the first envelope must be non-empty before its type
    // byte is read (and before raw.back()[0] below). A block with NO deposit at
    // all stays a hard reject: the L1-attributes deposit seeds the block's
    // fee/DA context and deposits[0] is read below.
    if (rawTxBytes[0].empty() || rawTxBytes[0][0] != OP_DEPOSIT_TX_TYPE || deposits.empty())
        throw bcos::evm::OpConsensusError("op block: no deposit transaction to seed the block");
    // First deposit is not L1 attributes: warn only. op-geth/op-reth accept this
    // at validation.
    if (!isOpEthL1AttributesTx(deposits[0]))
        BCOS_LOG(WARNING) << LOG_BADGE("OP_BLOCK_EXEC")
                          << "op block: first tx is a deposit but not the L1 attributes tx — "
                             "accepted";
    if (spec.has_da_footprint)
    {
        auto const& data = deposits[0].data;
        // Last-tx-only deposits-only check matches op-geth CalcDAFootprint
        // (core/types/rollup_cost.go:563-577): iterating every envelope would be
        // stricter than the reference client. Empty trailing envelope is treated
        // as non-deposit.
        bool const lastTxIsDeposit =
            !rawTxBytes.back().empty() && rawTxBytes.back()[0] == OP_DEPOSIT_TX_TYPE;
        try
        {
            validateOpEthJovianShape(
                std::span<uint8_t const>{data.data(), data.size()}, lastTxIsDeposit, spec);
        }
        catch (OpEthBlockError const& e)
        {
            throw bcos::evm::OpConsensusError(e.what());
        }
        if (auto scalar = opEthJovianDaFootprintGasScalar(
                std::span<uint8_t const>{data.data(), data.size()}))
            daFootprintGasScalar = *scalar;
    }
    co_return;
}

/// Block-level finalization shared by the two production drivers (mirror of
/// finalizeOpBlockResult): no-reward finalizeState → txTypes rebuild → hashErr
/// check → normalizeReceipts → MessagePasser snapshot → seal → stateRoot →
/// txRoot. txTypes are rebuilt from rawTxBytes[i][0] (the FISCO receipt has no
/// tx-type slot; the receipts-root leaf needs the EIP-2718 type byte).
///
/// @p errorSlot is the executor's block-wide slot: a poisoned slot here means a
/// per-tx storage read fault was swallowed into a default value (EthereumState's
/// fail-safe reads) — a storage fault, -32603.
/// @p skipStateRootBuild (incremental MPT): when true, the full rebuild is
/// skipped and the result's stateRoot is left EMPTY — the caller replaces it
/// with the incremental buildAndCollect/computeMptStateDelta root over the
/// block delta. When false the root is the full rebuild from the empty root
/// (root-only; node persistence lives in the incremental builders, not here).
template <class Storage, class RawTxRange>
task::Task<OpEthExecuteBlockResult> finalizeOpEthBlockResult(Storage& view,
    bcos::protocol::BlockHeader const& header, bcos::ledger::LedgerConfig const& ledgerConfig,
    OpForkSpec const& spec, std::shared_ptr<OpStorageErrorSlot> const& errorSlot,
    std::vector<bcos::protocol::TransactionReceipt::Ptr> receipts, RawTxRange const& rawTxBytes,
    int64_t cumulative, std::optional<std::string> const& hashErr, bool skipStateRootBuild)
{
    // End-of-block finalize — no block reward, no withdrawals (OP). Same
    // all-or-nothing journaling discipline as the per-tx loop: roll back on a
    // part-way failure (ported finalizeOpBlock / executeOpEthBlock step 4).
    {
        evmc::address coinbase{};
        auto const& cb = header.coinbase();
        if (cb.size() == sizeof(coinbase.bytes))
            std::copy_n(cb.begin(), sizeof(coinbase.bytes), coinbase.bytes);
        executor_v1::Rollbackable<Storage> rollable(view);
        eth::EthereumState<executor_v1::Rollbackable<Storage>> state(rollable);
        auto const savepoint = rollable.current();
        std::exception_ptr failure;
        try
        {
            co_await eth::finalizeState(state, spec.rev, coinbase, std::nullopt, {});
        }
        catch (...)
        {
            failure = std::current_exception();
        }
        if (failure)
        {
            try
            {
                co_await rollable.rollback(savepoint);
            }
            catch (...)
            {
            }
            std::rethrow_exception(failure);
        }
    }

    // Length guard: sealOpEthBlock iterates receipts and indexes txTypes[i] — a
    // caller passing mismatched lengths would read out of bounds.
    // Internal-invariant guard: a caller programming error, not a block-content
    // rejection, hence std::logic_error rather than OpConsensusError (as legacy).
    if (rawTxBytes.size() != receipts.size())
        throw std::logic_error("op block: receipts/rawTxBytes length mismatch (caller bug)");
    std::vector<uint8_t> txTypes;
    txTypes.reserve(rawTxBytes.size());
    for (std::size_t i = 0; i < rawTxBytes.size(); ++i)
    {
        if (rawTxBytes[i].empty())  // defensive: the per-tx loop already rejects these
            throw std::logic_error("op block: empty envelope (caller bug)");
        txTypes.emplace_back(opEthClassifyTxType(rawTxBytes[i][0]));
    }

    OpEthBlockResult result;
    result.receipts = std::move(receipts);
    result.txTypes = std::move(txTypes);
    result.gasUsed = cumulative;
    if (hashErr.has_value())
        throw bcos::evm::engine::OpStorageError("block-hash lookup failed: " + *hashErr);

    // One receipt-field policy for both receipts-root producers:
    // transactionIndex / logIndex are written, logsBloom is recomputed
    // unconditionally from logEntries, and cumulativeGasUsed is filled when
    // empty (the OP running prefix is set upstream in ExecuteContext::finish).
    bcos::protocol::normalizeReceipts(result.receipts);

    // Commitments: MessagePasser snapshot (the complete, tombstone-filtered live
    // slot map) → poison check → seal → stateRoot → txRoot. sealOpEthBlock's
    // OpEthBlockError (receipt-shape faults on self-produced receipts) is a
    // consensus-side reject, as the legacy sealOpBlock's OpConsensusError was.
    auto mpStorage = co_await opEthMessagePasserStorage(view);
    if (errorSlot && errorSlot->poisoned())
        throw bcos::evm::engine::OpStorageError("poisoned: " + errorSlot->firstErrorMessage());
    OpEthBlockSeal seal;
    try
    {
        seal = sealOpEthBlock(result, spec, mpStorage);
    }
    catch (OpEthBlockError const& e)
    {
        throw bcos::evm::OpConsensusError(e.what());
    }
    bcos::h256 stateRoot;
    if (!skipStateRootBuild)
    {
        // Full rebuild from the empty root. The lane pin matters:
        // computeMptStateDelta's ethLane (l2Mode) branches on executorVersion >=
        // ETHEREUM_EXECUTOR_VERSION, and the caller's config carries only the
        // EVM revision (OpScheduler::execute builds it revision-only) — default
        // version 0 would take the L1-shape branch. OPSTACK_EXECUTOR_VERSION is
        // the same pin loadLedgerConfig/verifyAndCommit use.
        auto rootConfig = ledgerConfig;
        rootConfig.setExecutorVersion(bcos::ledger::OPSTACK_EXECUTOR_VERSION);
        stateRoot = co_await bcos::ledger::mpt::computeMptStateRoot(
            view, bcos::ledger::mpt::emptyRootHash(), rootConfig);
    }
    auto txRoot = computeOpEthTransactionsRoot(rawTxBytes);
    co_return OpEthExecuteBlockResult{std::move(result.receipts), seal, stateRoot,
        static_cast<uint64_t>(cumulative), txRoot};
}
}  // namespace bcos::executor_v1::opstack
