// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// OpSchedulerImpl — the engine-facing seam shim for OP mode. `executeBlock` satisfies the
// scheduler_v1::TransactionScheduler concept check only (OP mode never calls it — throws
// immediately); OP block execution goes through the delegate's runOpBlockInjection. Every other
// method re-publishes the seam surface from OpErrors.h / OpBlockExecute.h so the engine reaches it
// as a dependent name on `SchedulerType`. A pure template header, no .cpp.

#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <opstack-executor/OpBlockExecute.h>  // computeOpTxRoot / announcedCommitmentsOf
#include <opstack-executor/OpErrors.h>  // OpBlockCommitments / commitmentsOf / mismatchedFieldOf
#include <cstdint>
#include <optional>
#include <range/v3/range/concepts.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace bcos::evm::engine
{

/// The OP block-execution environment was folded into `protocol::BlockHeader` (it now carries all
/// former OpBlockEnv fields); the engine fills one header object and the decoders read the
/// accessors directly.

namespace detail
{
}  // namespace detail
/// OP scheduler component: a pure engine-facing seam shim, constructed once per fork-timestamps
/// combination (composition-root-owned). It only re-publishes the seam surface the engine reaches
/// as dependent names on `SchedulerType`.
template <class Storage>
class OpSchedulerImpl
{
public:
    explicit OpSchedulerImpl(bcos::evm::opstack::OpForkTimestamps forkTimestamps)
      : m_forkTimestamps(forkTimestamps)
    {}

    // ---- engine-facing seam surface ----
    //
    // The engine's newPayload OP branch reaches every name below as a dependent name on its
    // `SchedulerType` template parameter — the only channel available (the engine must not include
    // anything from bcos-evm). The definitions live in OpErrors.h / OpBlockExecute.h; this block
    // re-publishes them under the class scope the engine can reach.

    /// The block-execution environment the engine fills in from the payload — the FISCO
    /// protocol::BlockHeader itself.
    using BlockEnv = bcos::protocol::BlockHeader;
    /// What executeOpBlock returns.
    using ExecuteResult = OpExecuteBlockResult;
    /// Consensus-level rejection → engine maps to INVALID.
    using ConsensusError = OpConsensusError;
    /// Storage-layer failure → engine maps to JSON-RPC -32603, never INVALID.
    using StorageError = OpStorageError;
    /// s_eth_hash_2_rawtx. No longer written (registerOpBlock writes SYS_HASH_2_TX via
    /// opEnvelopeToTars); kept only for read-side test assertions.
    static constexpr std::string_view c_ethRawTxTable = SYS_ETH_HASH_2_RAWTX;

    /// The six-way comparison surface (plus the two seal-only outputs) in bcos:: types.
    static OpBlockCommitments commitmentsOf(const OpExecuteBlockResult& result)
    {
        return bcos::evm::engine::commitmentsOf(
            result.seal, result.stateRoot, result.gasUsed, result.txRoot);
    }

    /// Announced-side projection for the six-field comparison (re-published as a dependent name).
    static bcos::evm::engine::OpBlockCommitments announcedCommitmentsOf(
        const bcos::engine::ExecutionPayload& payload, const bcos::h256& transactionsRoot,
        const bcos::protocol::BlockHeader& ethHeader)
    {
        return bcos::evm::engine::announcedCommitmentsOf(payload, transactionsRoot, ethHeader);
    }

    /// First mismatching field name, or nullopt (re-published as a dependent name).
    static std::optional<std::string> mismatchedFieldOf(
        const OpBlockCommitments& computed, const OpBlockCommitments& announced)
    {
        return bcos::evm::engine::mismatchedFieldOf(computed, announced);
    }

    /// transactionsRoot over raw EIP-2718 envelopes — the engine needs it before execution to
    /// reconstruct the header for the blockHash check (ExecutionPayload carries no such field).
    static bcos::h256 computeTxRoot(::ranges::input_range auto const& rawTxBytes)
    {
        return computeOpTxRoot(rawTxBytes);
    }

    /// Isthmus activation predicate for the engine's -38005 gate. Separate from configAt on
    /// purpose: configAt resolves sub-isthmus timestamps to the Isthmus config too, whereas the
    /// gate must reject them outright. Both read the same m_forkTimestamps.isthmusTime.
    [[nodiscard]] bool isIsthmusActiveAt(uint64_t timestamp) const noexcept
    {
        return timestamp >= m_forkTimestamps.isthmusTime;
    }

    /// Jovian activation predicate. The engine needs it for one fork-dependent static check: the
    /// header's blobGasUsed slot must be 0 under Isthmus, but from Jovian on it is the DA
    /// footprint, validated by seal comparison instead.
    [[nodiscard]] bool isJovianActiveAt(uint64_t timestamp) const noexcept
    {
        return timestamp >= m_forkTimestamps.jovianTime;
    }

    OpSchedulerImpl(const OpSchedulerImpl&) = delete;
    OpSchedulerImpl(OpSchedulerImpl&&) = delete;
    OpSchedulerImpl& operator=(const OpSchedulerImpl&) = delete;
    OpSchedulerImpl& operator=(OpSchedulerImpl&&) = delete;
    ~OpSchedulerImpl() = default;

    /// Concept-check only — OP mode never calls this. Throws before any co_await/co_return: safe
    /// because the Task coroutine body does not run until the coroutine is actually resumed.
    task::Task<std::vector<bcos::protocol::TransactionReceipt::Ptr>> executeBlock(
        Storage& /*storage*/, auto& /*executor*/,
        bcos::protocol::BlockHeader const& /*blockHeader*/,
        ::ranges::input_range auto const& /*transactions*/,
        bcos::ledger::LedgerConfig const& /*ledgerConfig*/)
    {
        throw std::logic_error("OpSchedulerImpl::executeBlock: not supported in OP mode");
        co_return {};  // unreachable; satisfies the coroutine's declared return type
    }

private:
    bcos::evm::opstack::OpForkTimestamps m_forkTimestamps;
};

}  // namespace bcos::evm::engine
