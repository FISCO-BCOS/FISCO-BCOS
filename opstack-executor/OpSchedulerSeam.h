// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// OpSchedulerSeam — the engine-facing seam shim for OP mode. `executeBlock` exists only to
// satisfy the scheduler_v1::TransactionScheduler concept check (OP mode never calls it — throws
// immediately). A pure template header, no .cpp.

#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <opstack-executor/OpBlockExecute.h>  // computeOpTxRoot / announcedCommitmentsOf
#include <opstack-executor/OpCommitments.h>  // OpBlockCommitments / commitmentsOf / mismatchedFieldOf
#include <opstack-executor/OpCommon.h>
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
/// OP scheduler component: a pure engine-facing seam shim, constructed once per OpForkFlags
/// combination (composition-root-owned). It only re-publishes the seam surface the engine reaches
/// as dependent names on `SchedulerType`.
template <class Storage>
class OpSchedulerSeam
{
public:
    explicit OpSchedulerSeam(bcos::evm::opstack::OpForkFlags forkFlags) : m_forkFlags(forkFlags) {}

    // ---- engine-facing seam surface ----
    //
    // The engine's newPayload OP branch reaches every name below as a dependent name on its
    // `SchedulerType` template parameter — the only channel available (the engine must not include
    // anything from bcos-evm). The definitions live in OpCommon.h / OpBlockExecute.h; this block
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

    /// Jovian activation predicate (feature-op_jovian). The engine needs it for fork-dependent
    /// static checks: the header's blobGasUsed slot must be 0 under Isthmus, but under Jovian it is
    /// the DA footprint (validated by seal comparison instead); base-fee derivation branches on it.
    /// There is no isIsthmusActiveAt anymore — OP mode itself IS the Isthmus+ admission check
    /// (executor_version>=3), and the -38005 gate no longer re-derives the fork from a timestamp.
    [[nodiscard]] bool isJovianActive() const noexcept { return m_forkFlags.jovianActive; }

    OpSchedulerSeam(const OpSchedulerSeam&) = delete;
    OpSchedulerSeam(OpSchedulerSeam&&) = delete;
    OpSchedulerSeam& operator=(const OpSchedulerSeam&) = delete;
    OpSchedulerSeam& operator=(OpSchedulerSeam&&) = delete;
    ~OpSchedulerSeam() = default;

    /// Concept-check only — OP mode never calls this. Throws before any co_await/co_return: safe
    /// because the Task coroutine body does not run until the coroutine is actually resumed.
    task::Task<std::vector<bcos::protocol::TransactionReceipt::Ptr>> executeBlock(
        Storage& /*storage*/, auto& /*executor*/,
        bcos::protocol::BlockHeader const& /*blockHeader*/,
        ::ranges::input_range auto const& /*transactions*/,
        bcos::ledger::LedgerConfig const& /*ledgerConfig*/)
    {
        throw std::logic_error("OpSchedulerSeam::executeBlock: not supported in OP mode");
        co_return {};  // unreachable; satisfies the coroutine's declared return type
    }

private:
    bcos::evm::opstack::OpForkFlags m_forkFlags;
};

}  // namespace bcos::evm::engine
