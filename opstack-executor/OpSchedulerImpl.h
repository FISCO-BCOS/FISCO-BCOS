// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// OpSchedulerImpl — the engine-facing seam shim for OP mode. `executeBlock` satisfies the
// scheduler_v1::TransactionScheduler concept check only (the concept is unconditional; OP mode
// never calls it — throws immediately). OP block execution goes through the delegate's
// runOpBlockInjection. Every other method
// re-publishes the seam surface from OpEngineSeam.h so the engine reaches it as a dependent name
// on `SchedulerType`.
//
// Layering: a pure template header (same shape as Storage2State.h) — depends on bcos-framework
// (Storage template parameter is instantiated against storage2/MultiLayerStorage::ViewType,
// protocol:: types); header-only, no .cpp.

#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <opstack-executor/OpEngineSeam.h>
#include <opstack-executor/OpErrors.h>
#include <opstack-executor/OpRlpDecode.h>
#include <cstdint>
#include <optional>
#include <range/v3/range/concepts.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace bcos::evm::engine
{

/// OP block execution environment was folded into `protocol::BlockHeader` when
/// PR #5385 gave the FISCO header tars slots for all 8 former OpBlockEnv fields (prevRandao/baseFee
/// -> coinbase/baseFee/prevRandao/parentBeaconBlockRoot/gasLimit/extraData/blobGasUsed/parentHash
/// -> parentInfo). The engine now fills one header object and `executeOpBlock`/`toBlockInfo` read
/// the accessors directly.

namespace detail
{
// Decode primitives (conversions / RLP scalars / composite decoders) live in
// OpRlpDecode.h; tx-type decoders + canonical round-trip in OpTxDecode.h.
// (The class template below references detail::decodeOneRawTx etc.)
}  // namespace detail
/// OP scheduler component: a pure engine-facing seam shim. Constructed once per fork-timestamps
/// combination (composition-root-owned). OP block execution goes through the delegate's
/// runOpBlockInjection; this class only re-publishes the
/// seam surface the engine reaches as dependent names on `SchedulerType`.
template <class Storage>
class OpSchedulerImpl
{
public:
    explicit OpSchedulerImpl(bcos::evm::opstack::OpForkTimestamps forkTimestamps)
      : m_forkTimestamps(forkTimestamps)
    {}

    // ---- engine-facing seam surface ----
    //
    // The engine's newPayload OP branch reaches every name below as a **dependent name on its
    // `SchedulerType` template parameter** (`typename SchedulerType::BlockEnv`,
    // `SchedulerType::computeTxRoot(...)`, ...) — the only channel available: engine must not
    // `#include` anything from bcos-evm (library purity, see the `c_opMode` comment in
    // EngineServiceImpl.h), and dependent names are looked up at instantiation, inside
    // `if constexpr (c_opMode)`, in a TU that has already included this header. The definitions
    // live in `OpEngineSeam.h`; this block only re-publishes them under the class scope the
    // engine can reach.

    /// The block-execution environment the engine fills in from the payload — the FISCO
    /// `protocol::BlockHeader` itself (PR #5385 gave every former OpBlockEnv field a tars slot).
    using BlockEnv = bcos::protocol::BlockHeader;
    /// What `executeOpBlock` returns.
    using ExecuteResult = OpExecuteBlockResult;
    /// Consensus-level rejection -> engine maps to INVALID.
    using ConsensusError = OpConsensusError;
    /// Storage-layer failure -> engine maps to JSON-RPC -32603, never INVALID.
    using StorageError = OpStorageError;
    /// c_ethRawTxTable = SYS_ETH_HASH_2_RAWTX (s_eth_hash_2_rawtx). No longer written:
    /// registerOpBlock writes SYS_HASH_2_TX via opEnvelopeToTars instead.
    /// The constant is kept only for read-side test assertions that the rawtx table is absent.
    static constexpr std::string_view c_ethRawTxTable = SYS_ETH_HASH_2_RAWTX;

    /// The six-way comparison surface (plus the two seal-only outputs) in bcos:: types.
    static OpBlockCommitments commitmentsOf(const OpExecuteBlockResult& result)
    {
        return bcos::evm::engine::commitmentsOf(
            result.seal, result.stateRoot, result.gasUsed, result.txRoot);
    }

    /// Announced-side projection for the six-field comparison: the payload's
    /// announced commitments, re-published from OpEngineSeam.h so the engine reaches it as a
    /// dependent name (MAIN seam-surface parity).
    static bcos::evm::engine::OpBlockCommitments announcedCommitmentsOf(
        const bcos::engine::ExecutionPayload& payload, const bcos::h256& transactionsRoot,
        const bcos::protocol::BlockHeader& ethHeader)
    {
        return bcos::evm::engine::announcedCommitmentsOf(payload, transactionsRoot, ethHeader);
    }

    /// Eight-field commitments comparison: returns the first mismatching field name, or nullopt.
    /// Re-published from OpEngineSeam.h so the engine reaches it as a dependent name.
    static std::optional<std::string> mismatchedFieldOf(
        const OpBlockCommitments& computed, const OpBlockCommitments& announced)
    {
        return bcos::evm::engine::mismatchedFieldOf(computed, announced);
    }

    /// transactionsRoot over raw EIP-2718 envelopes — the engine needs it *before* execution to
    /// reconstruct the header for the blockHash check (`ExecutionPayload` carries no
    /// transactionsRoot field); the delegate derives the same value.
    static bcos::h256 computeTxRoot(::ranges::input_range auto const& rawTxBytes)
    {
        return computeOpTxRoot(rawTxBytes);
    }

    /// Isthmus activation predicate for the engine's -38005 timestamp x version gate. The
    /// threshold comparison deliberately lives on this side of the seam, next to `configAt`,
    /// rather than being reimplemented in the engine.
    ///
    /// It is a *separate* function from `configAt` on purpose: `configAt` cannot answer this
    /// question — it resolves sub-`isthmusTime` timestamps to the Isthmus config as well
    /// (documented in OpForkSchedule.h: "Timestamps below isthmusTime also resolve to Isthmus"),
    /// because the minimal loop has no pre-Isthmus config to fall back to. The version gate, by
    /// contrast, must reject pre-Isthmus timestamps outright (-38005 on any version), so it needs
    /// the raw threshold. Both read the same injected `m_forkTimestamps.isthmusTime`, so there is
    /// still exactly one source of truth for the value.
    [[nodiscard]] bool isIsthmusActiveAt(uint64_t timestamp) const noexcept
    {
        return timestamp >= m_forkTimestamps.isthmusTime;
    }

    /// Jovian activation predicate. The engine needs it for one fork-dependent static check: the
    /// header's `blobGasUsed` slot must be 0 under Isthmus, but from Jovian on the same slot is
    /// repurposed as the DA footprint and is validated by seal comparison instead
    /// (OpBlockExecute.h). Same "threshold comparison stays on this side" reasoning as
    /// `isIsthmusActiveAt`.
    [[nodiscard]] bool isJovianActiveAt(uint64_t timestamp) const noexcept
    {
        return timestamp >= m_forkTimestamps.jovianTime;
    }

    OpSchedulerImpl(const OpSchedulerImpl&) = delete;
    OpSchedulerImpl(OpSchedulerImpl&&) = delete;
    OpSchedulerImpl& operator=(const OpSchedulerImpl&) = delete;
    OpSchedulerImpl& operator=(OpSchedulerImpl&&) = delete;
    ~OpSchedulerImpl() = default;

    /// Dummy signature satisfying scheduler_v1::TransactionScheduler (concept-check purpose only
    /// — the concept is an unconditional compile-time constraint on EngineServiceImpl's
    /// SchedulerType template parameter, independent of runtime reachability; OP mode never calls
    /// this). Throws immediately, before any co_await/co_return: safe because bcos::task::Task's
    /// promise_type uses std::suspend_always at initial_suspend (libtask/bcos-task/Task.h:55), so
    /// the coroutine body does not run until the coroutine is actually resumed (syncWait/co_await);
    /// unhandled_exception() (Task.h:63-71) is the standard propagation path for exceptions raised
    /// inside a Task<T> coroutine body.
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
